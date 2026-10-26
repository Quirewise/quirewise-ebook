#include "../../core/format_registry.h"
#include "../../core/text_buffer.h"
#include "fb2_format.h"
#include "fb2_image_resources.h"

#include "../../core/xml_parser.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fb2_local_name(const char *name)
{
    const char *colon;
    if (!name) return "";
    colon = strrchr(name, ':');
    return colon ? colon + 1u : name;
}

static const char *fb2_attribute_local(const QwXmlNode *node,
                                       const char *local_name)
{
    size_t i;
    if (!node || !local_name) return NULL;
    for (i = 0u; i < qw_xml_attribute_count(node); ++i) {
        const char *name = qw_xml_attribute_local_name(node, i);
        if (name && strcmp(name, local_name) == 0)
            return qw_xml_attribute_value(node, i);
    }
    return NULL;
}

static int fb2_html_append_escaped(QwTextBuffer *buffer,
                                  const char *text, size_t size)
{
    size_t i;
    for (i = 0u; i < size; ++i) {
        const char *replacement = NULL;
        size_t replacement_size = 0u;
        switch ((unsigned char)text[i]) {
        case '&': replacement = "&amp;"; replacement_size = 5u; break;
        case '<': replacement = "&lt;"; replacement_size = 4u; break;
        case '>': replacement = "&gt;"; replacement_size = 4u; break;
        case '"': replacement = "&quot;"; replacement_size = 6u; break;
        default: break;
        }
        if (replacement) {
            if (!qw_text_buffer_append(buffer, replacement, replacement_size)) return 0;
        } else if (!qw_text_buffer_append(buffer, text + i, 1u)) {
            return 0;
        }
    }
    return 1;
}

static const char *fb2_html_tag(const char *source_tag, QwXmlNode *node)
{
    const char *tag = fb2_local_name(source_tag);
    if (strcmp(tag, "section") == 0) return "section";
    if (strcmp(tag, "subtitle") == 0) return "h3";
    if (strcmp(tag, "p") == 0 || strcmp(tag, "v") == 0) return "p";
    if (strcmp(tag, "strong") == 0) return "strong";
    if (strcmp(tag, "emphasis") == 0 || strcmp(tag, "text-author") == 0) return "em";
    if (strcmp(tag, "code") == 0) return "code";
    if (strcmp(tag, "sub") == 0) return "sub";
    if (strcmp(tag, "sup") == 0) return "sup";
    if (strcmp(tag, "a") == 0) return "a";
    if (strcmp(tag, "poem") == 0 || strcmp(tag, "epigraph") == 0 ||
        strcmp(tag, "cite") == 0) return "blockquote";
    if (strcmp(tag, "stanza") == 0 || strcmp(tag, "annotation") == 0) return "div";
    if (strcmp(tag, "date") == 0) return "span";
    if (strcmp(tag, "empty-line") == 0) return "br";
    if (strcmp(tag, "style") == 0) {
        const char *name = qw_xml_att(node, "name");
        if (name && strcmp(name, "bold") == 0) return "strong";
        if (name && strcmp(name, "italic") == 0) return "em";
        if (name && strcmp(name, "code") == 0) return "code";
    }
    return NULL;
}

static int fb2_render_nodes(QwTextBuffer *buffer, QwXmlNode *root,
                            QwXmlNode *nodes, size_t depth, int *render_error);

static int fb2_render_title(QwTextBuffer *buffer, QwXmlNode *root,
                            QwXmlNode *nodes, size_t depth,
                            int *render_error)
{
    QwXmlNode *node;
    if (depth > 128u) {
        *render_error = QW_EBOOK_ERR_FORMAT;
        return 0;
    }
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (tag && strcmp(fb2_local_name(tag), "p") == 0) {
            if (!qw_text_buffer_append(buffer, "<h2>", 4u) ||
                !fb2_render_nodes(buffer, root, qw_xml_down(node), depth + 1u,
                                  render_error) ||
                !qw_text_buffer_append(buffer, "</h2>", 5u)) return 0;
        } else if (!tag) {
            const char *text = qw_xml_text(node);
            if (!qw_text_buffer_append(buffer, "<h2>", 4u) ||
                (text && !fb2_html_append_escaped(buffer, text, strlen(text))) ||
                !qw_text_buffer_append(buffer, "</h2>", 5u)) return 0;
        } else if (!fb2_render_nodes(buffer, root, node, depth + 1u,
                                     render_error)) {
            return 0;
        }
    }
    return 1;
}

static int fb2_is_external_link(const char *href)
{
    return href && (strncmp(href, "http://", 7u) == 0 ||
                    strncmp(href, "https://", 8u) == 0 ||
                    strncmp(href, "mailto:", 7u) == 0);
}

static int fb2_render_nodes(QwTextBuffer *buffer, QwXmlNode *root,
                            QwXmlNode *nodes, size_t depth, int *render_error)
{
    QwXmlNode *node;
    if (depth > 128u) {
        *render_error = QW_EBOOK_ERR_FORMAT;
        return 0;
    }
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *source_tag = qw_xml_tag(node);
        if (!source_tag) {
            const char *text = qw_xml_text(node);
            if (text && !fb2_html_append_escaped(buffer, text, strlen(text)))
                return 0;
            continue;
        }
        if (strcmp(fb2_local_name(source_tag), "image") == 0) {
            if (!qw_fb2_append_embedded_image_html(buffer, root, node,
                                                   render_error))
                return 0;
            continue;
        }
        {
            const char *tag = fb2_html_tag(source_tag, node);
            if (strcmp(fb2_local_name(source_tag), "title") == 0) {
                if (!fb2_render_title(buffer, root, qw_xml_down(node),
                                      depth + 1u, render_error)) return 0;
                continue;
            }
            if (tag) {
                const char *href = NULL;
                if (!qw_text_buffer_append(buffer, "<", 1u) ||
                    !qw_text_buffer_append(buffer, tag, strlen(tag))) return 0;
                if (strcmp(tag, "a") == 0) {
                    href = qw_xml_att(node, "xlink:href");
                    if (!href) href = qw_xml_att(node, "href");
                    if (fb2_is_external_link(href) &&
                        (!qw_text_buffer_append(buffer, " href=\"", 7u) ||
                         !fb2_html_append_escaped(buffer, href, strlen(href)) ||
                         !qw_text_buffer_append(buffer, "\"", 1u))) return 0;
                }
                if (strcmp(tag, "br") == 0) {
                    if (!qw_text_buffer_append(buffer, "/> ", 3u)) return 0;
                    continue;
                }
                if (!qw_text_buffer_append(buffer, ">", 1u) ||
                    !fb2_render_nodes(buffer, root, qw_xml_down(node),
                                     depth + 1u, render_error) ||
                    !qw_text_buffer_append(buffer, "</", 2u) ||
                    !qw_text_buffer_append(buffer, tag, strlen(tag)) ||
                    !qw_text_buffer_append(buffer, ">", 1u)) return 0;
            } else if (!fb2_render_nodes(buffer, root, qw_xml_down(node),
                                         depth + 1u, render_error)) {
                return 0;
            }
        }
    }
    return 1;
}

static int fb2_render_bodies(QwTextBuffer *buffer, QwXmlNode *root,
                             QwXmlNode *nodes, size_t depth,
                             size_t *body_count, int *render_error)
{
    QwXmlNode *node;
    if (depth > 128u) {
        *render_error = QW_EBOOK_ERR_FORMAT;
        return 0;
    }
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (tag && strcmp(fb2_local_name(tag), "body") == 0) {
            if (!qw_text_buffer_append(buffer, "<section>", 9u) ||
                !fb2_render_nodes(buffer, root, qw_xml_down(node), depth + 1u,
                                  render_error) ||
                !qw_text_buffer_append(buffer, "</section>", 10u)) return 0;
            ++*body_count;
        } else if (!fb2_render_bodies(buffer, root, qw_xml_down(node),
                                      depth + 1u, body_count, render_error)) {
            return 0;
        }
    }
    return 1;
}

int qw_fb2_native_open_layout(const void *data, size_t size,
                              QwInputFormat input_format,
                              const QwConvertOptions *options,
                              QwLayoutDocument *out_document,
                              QwConvertReport *report)
{
    QwXmlNode *xml = NULL;
    QwXmlNode *root;
    QwTextBuffer html = {0};
    size_t body_count = 0u;
    int render_error = QW_EBOOK_ERR_OUT_OF_MEMORY;
    int rc = QW_EBOOK_ERR_FORMAT;
    static const char html_open[] =
        "<!doctype html><html><head><meta charset=\"utf-8\"></head><body>";
    (void)options;
    if (!out_document) return QW_EBOOK_ERR_INVALID_ARGUMENT;
    memset(out_document, 0, sizeof(*out_document));
    if (!data || !size || input_format != QW_FORMAT_FB2)
        return QW_EBOOK_ERR_INVALID_ARGUMENT;

    html.capacity = 1024u;
    html.limit = 64u * 1024u * 1024u + 1u;
    xml = qw_xml_parse(data, size, NULL);
    if (!xml) {
        if (report) snprintf(report->note, sizeof(report->note),
                             "FictionBook XML could not be parsed");
        goto fb2_done;
    }
    root = qw_xml_root(xml);
    if (!root || !qw_xml_tag(root) ||
        strcmp(fb2_local_name(qw_xml_tag(root)), "FictionBook") != 0) {
        if (report) snprintf(report->note, sizeof(report->note),
                             "XML root is not a FictionBook document");
        goto fb2_done;
    }
    if (!qw_text_buffer_append(&html, html_open, sizeof(html_open) - 1u) ||
        !fb2_render_bodies(&html, root, root, 0u, &body_count, &render_error)) {
        rc = render_error;
        if (report) snprintf(report->note, sizeof(report->note), "%s",
            rc == QW_EBOOK_ERR_UNSUPPORTED
                ? "FictionBook image reference or image type is unsupported"
                : rc == QW_EBOOK_ERR_FORMAT
                    ? "FictionBook contains malformed image data or structure"
                    : "Could not allocate FictionBook layout HTML");
        goto fb2_done;
    }
    if (!body_count) {
        rc = QW_EBOOK_ERR_FORMAT;
        if (report) snprintf(report->note, sizeof(report->note),
                             "FictionBook document has no body section");
        goto fb2_done;
    }
    if (!qw_text_buffer_append(&html, "</body></html>", 14u)) {
        rc = QW_EBOOK_ERR_OUT_OF_MEMORY;
        if (report) snprintf(report->note, sizeof(report->note),
                             "Could not allocate FictionBook layout HTML");
        goto fb2_done;
    }

    out_document->html_utf8 = (const uint8_t *)html.data;
    out_document->html_size = html.size;
    out_document->context = html.data;
    out_document->dispose = free;
    html.data = NULL;
    if (report) snprintf(report->note, sizeof(report->note),
                         "FictionBook XML parsed into an HTML-backed layout view");
    rc = QW_EBOOK_OK;
fb2_done:
    free(html.data);
    qw_xml_drop(xml);
    return rc;
}
