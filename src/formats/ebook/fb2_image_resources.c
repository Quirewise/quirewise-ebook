#include "fb2_image_resources.h"

#include "../../core/format_registry.h"

#include <stddef.h>
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

static QwXmlNode *fb2_find_binary(QwXmlNode *nodes, const char *id,
                                 size_t depth, size_t *matches)
{
    QwXmlNode *node;
    QwXmlNode *found = NULL;
    if (depth > 128u) return NULL;
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (tag && strcmp(fb2_local_name(tag), "binary") == 0) {
            const char *candidate_id = qw_xml_att(node, "id");
            if (candidate_id && strcmp(candidate_id, id) == 0) {
                ++*matches;
                found = node;
            }
        }
        {
            QwXmlNode *child = fb2_find_binary(qw_xml_down(node), id,
                                                depth + 1u, matches);
            if (child) found = child;
        }
    }
    return found;
}

static int fb2_ascii_equal_ci(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return *left == '\0' && *right == '\0';
}

static int fb2_base64_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int fb2_base64_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/';
}

static int fb2_binary_base64_valid(const QwXmlNode *binary)
{
    const QwXmlNode *node;
    size_t length = 0u;
    size_t padding = 0u;
    int saw_padding = 0;
    for (node = qw_xml_down((QwXmlNode *)binary); node;
         node = qw_xml_next((QwXmlNode *)node)) {
        const char *text = qw_xml_text(node);
        size_t i;
        if (!text) {
            if (qw_xml_tag(node)) return 0;
            continue;
        }
        for (i = 0u; text[i]; ++i) {
            unsigned char c = (unsigned char)text[i];
            if (fb2_base64_space(c)) continue;
            if (fb2_base64_char(c)) {
                if (saw_padding) return 0;
            } else if (c == '=') {
                saw_padding = 1;
                if (++padding > 2u) return 0;
            } else {
                return 0;
            }
            ++length;
        }
    }
    return length != 0u && (length & 3u) == 0u;
}

static const char *fb2_binary_mime(const QwXmlNode *binary)
{
    const char *content_type = qw_xml_att(binary, "content-type");
    if (!content_type) return NULL;
    if (fb2_ascii_equal_ci(content_type, "image/jpeg") ||
        fb2_ascii_equal_ci(content_type, "image/jpg")) return "image/jpeg";
    if (fb2_ascii_equal_ci(content_type, "image/png")) return "image/png";
    if (fb2_ascii_equal_ci(content_type, "image/gif")) return "image/gif";
    return NULL;
}

static int fb2_append_binary_base64(QwTextBuffer *buffer,
                                    const QwXmlNode *binary)
{
    const QwXmlNode *node;
    for (node = qw_xml_down((QwXmlNode *)binary); node;
         node = qw_xml_next((QwXmlNode *)node)) {
        const char *text = qw_xml_text(node);
        size_t start = 0u;
        size_t i;
        if (!text) continue;
        for (i = 0u; ; ++i) {
            unsigned char c = (unsigned char)text[i];
            if (!c || fb2_base64_space(c)) {
                if (i > start &&
                    !qw_text_buffer_append(buffer, text + start, i - start))
                    return 0;
                if (!c) break;
                start = i + 1u;
            }
        }
    }
    return 1;
}

int qw_fb2_append_embedded_image_html(QwTextBuffer *buffer, QwXmlNode *root,
                                      QwXmlNode *image, int *render_error)
{
    static const char image_prefix[] = "<img src=\"data:";
    static const char base64_marker[] = ";base64,";
    static const char image_suffix[] = "\" alt=\"\"/>";
    const char *href;
    const char *mime;
    QwXmlNode *binary;
    size_t matches = 0u;
    if (!buffer || !root || !image || !render_error)
        return 0;
    href = fb2_attribute_local(image, "href");
    if (!href || href[0] != '#' || href[1] == '\0') {
        *render_error = QW_EBOOK_ERR_UNSUPPORTED;
        return 0;
    }
    binary = fb2_find_binary(root, href + 1u, 0u, &matches);
    if (!binary || matches != 1u) {
        *render_error = QW_EBOOK_ERR_UNSUPPORTED;
        return 0;
    }
    mime = fb2_binary_mime(binary);
    if (!mime) {
        *render_error = QW_EBOOK_ERR_UNSUPPORTED;
        return 0;
    }
    if (!fb2_binary_base64_valid(binary)) {
        *render_error = QW_EBOOK_ERR_FORMAT;
        return 0;
    }
    if (!qw_text_buffer_append(buffer, image_prefix, sizeof(image_prefix) - 1u) ||
        !qw_text_buffer_append(buffer, mime, strlen(mime)) ||
        !qw_text_buffer_append(buffer, base64_marker, sizeof(base64_marker) - 1u) ||
        !fb2_append_binary_base64(buffer, binary) ||
        !qw_text_buffer_append(buffer, image_suffix, sizeof(image_suffix) - 1u))
        return 0;
    return 1;
}
