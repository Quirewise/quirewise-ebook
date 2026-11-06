#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "epub_native.h"

#include "../../core/xml_parser.h"
#include "../archive/zip_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

enum {
    QW_EPUB_MAX_XML_SIZE = 16 * 1024 * 1024,
    QW_EPUB_MAX_SPINE_ITEMS = 10000,
    QW_EPUB_MAX_LAYOUT_PARAGRAPHS = 1000000,
    QW_EPUB_MAX_LAYOUT_RUNS = 4000000,
    QW_EPUB_MAX_LAYOUT_TEXT_BYTES = 256 * 1024 * 1024
};

#define QW_EPUB_TOTAL_EXTRACT_LIMIT (2u * 1024u * 1024u * 1024u)

typedef struct QwEpubManifestItem {
    char *id;
    char *path;
} QwEpubManifestItem;

typedef struct QwEpubSpine {
    QwEpubManifestItem *items;
    size_t count;
    size_t capacity;
} QwEpubSpine;

typedef struct QwEpubLayoutOwner {
    QwLayoutSection *sections;
    size_t *paragraph_capacities;
    size_t section_count;
    size_t section_capacity;
    size_t paragraph_count;
    size_t run_count;
    size_t layout_text_bytes;
} QwEpubLayoutOwner;

typedef struct QwEpubParagraphBuilder {
    QwLayoutRun *runs;
    size_t run_count;
    size_t run_capacity;
    size_t text_bytes;
} QwEpubParagraphBuilder;

static int epub_set_error(QwConvertReport *report, int code,
                          const char *message)
{
    if (report && message) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
    return code;
}

static char *epub_strdup(const char *text)
{
    size_t size;
    char *copy;
    if (!text) return NULL;
    size = strlen(text);
    if (size == SIZE_MAX) return NULL;
    copy = (char *)malloc(size + 1u);
    if (copy) memcpy(copy, text, size + 1u);
    return copy;
}

static const char *epub_local_name(const char *name)
{
    const char *colon;
    if (!name) return "";
    colon = strrchr(name, ':');
    return colon ? colon + 1u : name;
}

static int epub_is_tag(QwXmlNode *node, const char *name)
{
    const char *tag = qw_xml_tag(node);
    return tag && strcmp(epub_local_name(tag), name) == 0;
}

static QwXmlNode *epub_find_descendant(QwXmlNode *node, const char *name,
                                    size_t depth)
{
    QwXmlNode *found;
    if (depth > 128u) return NULL;
    for (; node; node = qw_xml_next(node)) {
        if (epub_is_tag(node, name)) return node;
        found = epub_find_descendant(qw_xml_down(node), name, depth + 1u);
        if (found) return found;
    }
    return NULL;
}

static QwXmlNode *epub_parse_xml(const uint8_t *data, size_t size)
{
    return qw_xml_parse(data, size, NULL);
}

static int epub_hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static char *epub_resolve_href(const char *base_directory, const char *href)
{
    size_t base_size = base_directory ? strlen(base_directory) : 0u;
    size_t href_size;
    size_t decoded_size = 0;
    size_t combined_size;
    size_t offset;
    size_t component_offsets[4096];
    size_t component_count = 0;
    size_t cursor = 0;
    char *decoded;
    char *combined;
    char *result;
    if (!href || !*href) return NULL;
    for (offset = 0; href[offset] && href[offset] != '#' && href[offset] != '?'; ++offset) {
        if (href[offset] == '%') {
            int high, low;
            if (!href[offset + 1u] || !href[offset + 2u] ||
                (high = epub_hex_value((unsigned char)href[offset + 1u])) < 0 ||
                (low = epub_hex_value((unsigned char)href[offset + 2u])) < 0 ||
                ((high << 4) | low) == 0 || ((high << 4) | low) == '\\') {
                return NULL;
            }
            decoded_size++;
            offset += 2u;
        } else {
            if (href[offset] == '\\' || href[offset] == ':') return NULL;
            decoded_size++;
        }
    }
    href_size = offset;
    decoded = (char *)malloc(decoded_size + 1u);
    if (!decoded) return NULL;
    offset = 0;
    while (offset < href_size) {
        if (href[offset] == '%') {
            int high = epub_hex_value((unsigned char)href[offset + 1u]);
            int low = epub_hex_value((unsigned char)href[offset + 2u]);
            decoded[cursor++] = (char)((high << 4) | low);
            offset += 3u;
        } else {
            decoded[cursor++] = href[offset++];
        }
    }
    decoded[cursor] = '\0';
    if (!cursor || decoded[0] == '/' || decoded[0] == '\\' ||
        base_size > SIZE_MAX - cursor - 2u) {
        free(decoded);
        return NULL;
    }
    combined_size = base_size + (base_size ? 1u : 0u) + cursor;
    combined = (char *)malloc(combined_size + 1u);
    result = (char *)malloc(combined_size + 1u);
    if (!combined || !result) {
        free(combined);
        free(result);
        free(decoded);
        return NULL;
    }
    cursor = 0;
    if (base_size) {
        memcpy(combined, base_directory, base_size);
        cursor = base_size;
        combined[cursor++] = '/';
    }
    memcpy(combined + cursor, decoded, strlen(decoded) + 1u);
    free(decoded);

    cursor = 0;
    offset = 0;
    while (offset <= combined_size) {
        size_t start = offset;
        size_t length;
        while (offset < combined_size && combined[offset] != '/') ++offset;
        length = offset - start;
        if (length == 0u || (length == 1u && combined[start] == '.')) {
            if (length == 0u && offset != combined_size) goto invalid;
        } else if (length == 2u && combined[start] == '.' &&
                   combined[start + 1u] == '.') {
            if (!component_count) goto invalid;
            cursor = component_offsets[--component_count];
        } else {
            size_t i;
            if (component_count >= sizeof(component_offsets) /
                                   sizeof(component_offsets[0])) goto invalid;
            for (i = start; i < start + length; ++i) {
                if (combined[i] == ':' || combined[i] == '\\' ||
                    combined[i] == '\0') goto invalid;
            }
            component_offsets[component_count++] = cursor;
            if (cursor) result[cursor++] = '/';
            memcpy(result + cursor, combined + start, length);
            cursor += length;
        }
        if (offset == combined_size) break;
        ++offset;
    }
    if (!cursor) goto invalid;
    result[cursor] = '\0';
    free(combined);
    return result;

invalid:
    free(combined);
    free(result);
    return NULL;
}

static char *epub_base_directory(const char *path)
{
    const char *slash;
    size_t size;
    char *directory;
    if (!path) return NULL;
    slash = strrchr(path, '/');
    if (!slash) return epub_strdup("");
    size = (size_t)(slash - path);
    directory = (char *)malloc(size + 1u);
    if (!directory) return NULL;
    memcpy(directory, path, size);
    directory[size] = '\0';
    return directory;
}

static void *epub_extract_named(QwZipArchive *zip, const char *name,
                                size_t *out_size)
{
    size_t index = qw_zip_reader_locate_file(zip, name, NULL,
                                              QW_ZIP_CASE_SENSITIVE);
    if (index == (size_t)-1 ||
        !qw_zip_reader_is_file_supported(zip, index)) return NULL;
    return qw_zip_reader_extract_to_heap(zip, index, out_size, 0u);
}

static int epub_spine_append(QwEpubSpine *spine, const char *id,
                             const char *path)
{
    QwEpubManifestItem *grown;
    char *id_copy;
    char *path_copy;
    size_t capacity;
    if (spine->count >= QW_EPUB_MAX_SPINE_ITEMS) return 0;
    if (spine->count == spine->capacity) {
        capacity = spine->capacity ? spine->capacity * 2u : 32u;
        if (capacity > QW_EPUB_MAX_SPINE_ITEMS) capacity = QW_EPUB_MAX_SPINE_ITEMS;
        if (capacity > SIZE_MAX / sizeof(*spine->items)) return 0;
        grown = (QwEpubManifestItem *)realloc(spine->items,
                                             capacity * sizeof(*spine->items));
        if (!grown) return 0;
        spine->items = grown;
        spine->capacity = capacity;
    }
    id_copy = id ? epub_strdup(id) : epub_strdup("");
    path_copy = epub_strdup(path);
    if (!id_copy || !path_copy) {
        free(id_copy);
        free(path_copy);
        return 0;
    }
    spine->items[spine->count].id = id_copy;
    spine->items[spine->count].path = path_copy;
    ++spine->count;
    return 1;
}

static void epub_spine_free(QwEpubSpine *spine)
{
    size_t i;
    if (!spine) return;
    for (i = 0; i < spine->count; ++i) {
        free(spine->items[i].id);
        free(spine->items[i].path);
    }
    free(spine->items);
    memset(spine, 0, sizeof(*spine));
}

static int epub_collect_spine(QwXmlNode *opf_root, const char *opf_path,
                              QwEpubSpine *out_spine)
{
    QwXmlNode *manifest = epub_find_descendant(opf_root, "manifest", 0u);
    QwXmlNode *spine_node = epub_find_descendant(opf_root, "spine", 0u);
    QwXmlNode *item;
    char *base_directory;
    QwEpubSpine manifest_items = {0};
    int ok = 0;
    if (!manifest || !spine_node) return 0;
    base_directory = epub_base_directory(opf_path);
    if (!base_directory) return 0;
    for (item = qw_xml_down(manifest); item; item = qw_xml_next(item)) {
        const char *id;
        const char *href;
        char *path;
        if (!epub_is_tag(item, "item")) continue;
        id = qw_xml_att(item, "id");
        href = qw_xml_att(item, "href");
        if (!id || !href) continue;
        path = epub_resolve_href(base_directory, href);
        if (!path || !epub_spine_append(&manifest_items, id, path)) {
            free(path);
            goto done;
        }
        free(path);
    }
    for (item = qw_xml_down(spine_node); item; item = qw_xml_next(item)) {
        const char *idref;
        size_t i;
        if (!epub_is_tag(item, "itemref")) continue;
        idref = qw_xml_att(item, "idref");
        if (!idref) continue;
        for (i = 0; i < manifest_items.count; ++i) {
            if (strcmp(manifest_items.items[i].id, idref) == 0) break;
        }
        if (i == manifest_items.count ||
            !epub_spine_append(out_spine, idref,
                               manifest_items.items[i].path)) goto done;
    }
    ok = out_spine->count != 0u;

done:
    free(base_directory);
    epub_spine_free(&manifest_items);
    return ok;
}

static void epub_layout_owner_dispose(void *opaque)
{
    QwEpubLayoutOwner *owner = (QwEpubLayoutOwner *)opaque;
    size_t section_index;
    if (!owner) return;
    for (section_index = 0; section_index < owner->section_count;
         ++section_index) {
        QwLayoutSection *section = &owner->sections[section_index];
        QwLayoutParagraph *paragraphs =
            (QwLayoutParagraph *)section->paragraphs;
        size_t paragraph_index;
        for (paragraph_index = 0; paragraph_index < section->paragraph_count;
             ++paragraph_index) {
            QwLayoutParagraph *paragraph = &paragraphs[paragraph_index];
            QwLayoutRun *runs = (QwLayoutRun *)paragraph->runs;
            size_t run_index;
            for (run_index = 0; run_index < paragraph->run_count; ++run_index) {
                free((void *)runs[run_index].utf8_text);
                free((void *)runs[run_index].link_target);
            }
            free(runs);
        }
        free(paragraphs);
        free((void *)section->title);
    }
    free(owner->sections);
    free(owner->paragraph_capacities);
    free(owner);
}

static QwLayoutSection *epub_layout_section_append(QwEpubLayoutOwner *owner)
{
    QwLayoutSection *grown;
    size_t *grown_capacities;
    size_t capacity;
    if (owner->section_count == owner->section_capacity) {
        capacity = owner->section_capacity ? owner->section_capacity * 2u : 8u;
        if (capacity > QW_EPUB_MAX_SPINE_ITEMS) {
            capacity = QW_EPUB_MAX_SPINE_ITEMS;
        }
        if (capacity <= owner->section_count ||
            capacity > SIZE_MAX / sizeof(*owner->sections) ||
            capacity > SIZE_MAX / sizeof(*owner->paragraph_capacities)) return NULL;
        grown_capacities = (size_t *)realloc(owner->paragraph_capacities,
            capacity * sizeof(*owner->paragraph_capacities));
        if (!grown_capacities) return NULL;
        owner->paragraph_capacities = grown_capacities;
        grown = (QwLayoutSection *)realloc(owner->sections,
                                           capacity * sizeof(*owner->sections));
        if (!grown) return NULL;
        owner->sections = grown;
        owner->section_capacity = capacity;
    }
    owner->paragraph_capacities[owner->section_count] = 0u;
    memset(&owner->sections[owner->section_count], 0,
           sizeof(owner->sections[owner->section_count]));
    return &owner->sections[owner->section_count++];
}

static void epub_layout_paragraph_builder_dispose(
    QwEpubParagraphBuilder *builder)
{
    size_t i;
    if (!builder) return;
    for (i = 0; i < builder->run_count; ++i) {
        free((void *)builder->runs[i].utf8_text);
        free((void *)builder->runs[i].link_target);
    }
    free(builder->runs);
    memset(builder, 0, sizeof(*builder));
}

static char *epub_layout_normalize_text(const char *text, int has_previous,
                                        int previous_ends_with_space,
                                        size_t *out_size)
{
    size_t input_size;
    size_t i;
    size_t used = 0u;
    int pending_space = 0;
    char *copy;
    if (!text || !out_size) return NULL;
    *out_size = 0u;
    input_size = strlen(text);
    if (input_size == SIZE_MAX) {
        *out_size = SIZE_MAX;
        return NULL;
    }
    copy = (char *)malloc(input_size + 1u);
    if (!copy) {
        *out_size = SIZE_MAX;
        return NULL;
    }
    for (i = 0; i < input_size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (isspace(ch)) {
            pending_space = 1;
            continue;
        }
        if (pending_space && (used != 0u ||
            (has_previous && !previous_ends_with_space))) {
            copy[used++] = ' ';
        }
        pending_space = 0;
        copy[used++] = (char)ch;
    }
    if (pending_space && (used != 0u ||
        (has_previous && !previous_ends_with_space))) {
        copy[used++] = ' ';
    }
    copy[used] = '\0';
    if (used == 0u) {
        free(copy);
        return NULL;
    }
    *out_size = used;
    return copy;
}

static int epub_layout_run_append(QwEpubLayoutOwner *owner,
                                  QwEpubParagraphBuilder *builder,
                                  const char *text, unsigned style_flags,
                                  const char *link_target)
{
    QwLayoutRun *grown;
    size_t text_size = 0u;
    size_t capacity;
    char *normalized;
    int has_previous;
    int previous_ends_with_space = 0;
    char *link_copy = NULL;
    if (!owner || !builder || !text) return 1;
    has_previous = builder->text_bytes != 0u;
    if (has_previous && builder->run_count) {
        const QwLayoutRun *last = &builder->runs[builder->run_count - 1u];
        previous_ends_with_space = last->text_size != 0u &&
            last->utf8_text[last->text_size - 1u] == ' ';
    }
    normalized = epub_layout_normalize_text(text, has_previous,
        previous_ends_with_space, &text_size);
    if (!normalized) return text_size == SIZE_MAX ? 0 : 1;
    if (owner->layout_text_bytes > QW_EPUB_MAX_LAYOUT_TEXT_BYTES ||
        text_size > QW_EPUB_MAX_LAYOUT_TEXT_BYTES - owner->layout_text_bytes) {
        free(normalized);
        return 0;
    }
    if (link_target && *link_target) {
        size_t link_size = strlen(link_target);
        if (link_size == SIZE_MAX) {
            free(normalized);
            return 0;
        }
        link_copy = (char *)malloc(link_size + 1u);
        if (!link_copy) {
            free(normalized);
            return 0;
        }
        memcpy(link_copy, link_target, link_size + 1u);
    }
    if (builder->run_count) {
        QwLayoutRun *last = &builder->runs[builder->run_count - 1u];
        const char *last_link = last->link_target;
        int same_link = (!last_link && !link_copy) ||
            (last_link && link_copy && strcmp(last_link, link_copy) == 0);
        if (last->style_flags == style_flags && same_link &&
            text_size <= SIZE_MAX - last->text_size - 1u) {
            char *joined = (char *)realloc((void *)last->utf8_text,
                last->text_size + text_size + 1u);
            if (!joined) {
                free(normalized);
                free(link_copy);
                return 0;
            }
            memcpy(joined + last->text_size, normalized, text_size + 1u);
            last->utf8_text = (const uint8_t *)joined;
            last->text_size += text_size;
            builder->text_bytes += text_size;
            owner->layout_text_bytes += text_size;
            free(normalized);
            free(link_copy);
            return 1;
        }
    }
    if (owner->run_count >= QW_EPUB_MAX_LAYOUT_RUNS) {
        free(normalized);
        free(link_copy);
        return 0;
    }
    if (builder->run_count == builder->run_capacity) {
        capacity = builder->run_capacity ? builder->run_capacity * 2u : 8u;
        if (capacity > QW_EPUB_MAX_LAYOUT_RUNS ||
            capacity > SIZE_MAX / sizeof(*builder->runs)) {
            free(normalized);
            free(link_copy);
            return 0;
        }
        grown = (QwLayoutRun *)realloc(builder->runs,
                                       capacity * sizeof(*builder->runs));
        if (!grown) {
            free(normalized);
            free(link_copy);
            return 0;
        }
        builder->runs = grown;
        builder->run_capacity = capacity;
    }
    builder->runs[builder->run_count].utf8_text =
        (const uint8_t *)normalized;
    builder->runs[builder->run_count].text_size = text_size;
    builder->runs[builder->run_count].style_flags = style_flags;
    builder->runs[builder->run_count].link_target = link_copy;
    ++builder->run_count;
    ++owner->run_count;
    builder->text_bytes += text_size;
    owner->layout_text_bytes += text_size;
    return 1;
}

static unsigned epub_layout_style_for_tag(const char *tag,
                                          unsigned inherited)
{
    const char *name = epub_local_name(tag);
    if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
        inherited |= QW_LAYOUT_STYLE_BOLD;
    } else if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0 ||
               strcmp(name, "cite") == 0 || strcmp(name, "dfn") == 0 ||
               strcmp(name, "var") == 0) {
        inherited |= QW_LAYOUT_STYLE_ITALIC;
    } else if (strcmp(name, "code") == 0 || strcmp(name, "kbd") == 0 ||
               strcmp(name, "samp") == 0) {
        inherited |= QW_LAYOUT_STYLE_CODE;
    } else if (strcmp(name, "blockquote") == 0) {
        inherited |= QW_LAYOUT_STYLE_QUOTE;
    }
    return inherited;
}

static int epub_layout_external_link(const char *href)
{
    static const char *const schemes[] = {
        "http://", "https://", "mailto:", "tel:"
    };
    size_t i;
    if (!href) return 0;
    for (i = 0; i < sizeof(schemes) / sizeof(schemes[0]); ++i) {
        size_t j;
        for (j = 0; schemes[i][j] && href[j]; ++j) {
            unsigned char a = (unsigned char)schemes[i][j];
            unsigned char b = (unsigned char)href[j];
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
            if (a != b) break;
        }
        if (!schemes[i][j]) return 1;
    }
    return 0;
}

static int epub_layout_collect_inline(QwEpubLayoutOwner *owner,
                                      QwEpubParagraphBuilder *builder,
                                      QwXmlNode *nodes, unsigned style_flags,
                                      const char *link_target, size_t depth)
{
    QwXmlNode *node;
    if (depth > 128u) return QW_EBOOK_ERR_UNSUPPORTED;
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (!tag) {
            const char *text = qw_xml_text(node);
            if (text && !epub_layout_run_append(owner, builder, text,
                                                style_flags, link_target)) {
                return QW_EBOOK_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        if (strcmp(epub_local_name(tag), "script") == 0 ||
            strcmp(epub_local_name(tag), "style") == 0) continue;
        if (strcmp(epub_local_name(tag), "br") == 0) {
            if (!epub_layout_run_append(owner, builder, " ", style_flags,
                                        link_target)) return QW_EBOOK_ERR_OUT_OF_MEMORY;
            continue;
        }
        {
            unsigned child_flags = epub_layout_style_for_tag(tag, style_flags);
            const char *child_link = link_target;
            const char *href;
            if (strcmp(epub_local_name(tag), "a") == 0) {
                href = qw_xml_att(node, "href");
                if (epub_layout_external_link(href)) child_link = href;
            }
            {
                int rc = epub_layout_collect_inline(owner, builder,
                    qw_xml_down(node), child_flags, child_link, depth + 1u);
                if (rc != QW_EBOOK_OK) return rc;
            }
        }
    }
    return QW_EBOOK_OK;
}

static unsigned epub_layout_heading_level(const char *tag)
{
    const char *name = epub_local_name(tag);
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' &&
        name[2] == '\0') return (unsigned)(name[1] - '0');
    return 0u;
}

static int epub_layout_tag_is_block(const char *tag)
{
    const char *name = epub_local_name(tag);
    return strcmp(name, "p") == 0 || strcmp(name, "h1") == 0 ||
           strcmp(name, "h2") == 0 || strcmp(name, "h3") == 0 ||
           strcmp(name, "h4") == 0 || strcmp(name, "h5") == 0 ||
           strcmp(name, "h6") == 0 || strcmp(name, "li") == 0 ||
           strcmp(name, "pre") == 0 || strcmp(name, "dt") == 0 ||
           strcmp(name, "dd") == 0 || strcmp(name, "figcaption") == 0 ||
           strcmp(name, "address") == 0;
}

static int epub_layout_flush_paragraph(QwEpubLayoutOwner *owner,
                                       QwLayoutSection *section,
                                       QwEpubParagraphBuilder *builder,
                                       unsigned heading_level,
                                       unsigned flags)
{
    QwLayoutParagraph *paragraphs;
    QwLayoutParagraph *paragraph;
    size_t capacity;
    size_t section_index;
    if (!builder->text_bytes) {
        epub_layout_paragraph_builder_dispose(builder);
        return QW_EBOOK_OK;
    }
    if (owner->paragraph_count >= QW_EPUB_MAX_LAYOUT_PARAGRAPHS) {
        return QW_EBOOK_ERR_OUT_OF_MEMORY;
    }
    if (builder->run_count) {
        QwLayoutRun *last = &builder->runs[builder->run_count - 1u];
        char *text = (char *)last->utf8_text;
        while (last->text_size && text[last->text_size - 1u] == ' ') {
            text[--last->text_size] = '\0';
            --builder->text_bytes;
            --owner->layout_text_bytes;
        }
        if (!last->text_size) {
            free((void *)last->utf8_text);
            free((void *)last->link_target);
            --builder->run_count;
            --owner->run_count;
        }
    }
    if (!builder->text_bytes) {
        epub_layout_paragraph_builder_dispose(builder);
        return QW_EBOOK_OK;
    }
    if (section->paragraph_count >= QW_EPUB_MAX_LAYOUT_PARAGRAPHS) {
        return QW_EBOOK_ERR_OUT_OF_MEMORY;
    }
    section_index = (size_t)(section - owner->sections);
    if (section_index >= owner->section_count) return QW_EBOOK_ERR_FORMAT;
    if (section->paragraph_count == owner->paragraph_capacities[section_index]) {
        capacity = owner->paragraph_capacities[section_index]
            ? owner->paragraph_capacities[section_index] * 2u : 8u;
        if (capacity > QW_EPUB_MAX_LAYOUT_PARAGRAPHS) {
            capacity = QW_EPUB_MAX_LAYOUT_PARAGRAPHS;
        }
        if (capacity <= section->paragraph_count ||
            capacity > SIZE_MAX / sizeof(*paragraphs)) {
            return QW_EBOOK_ERR_OUT_OF_MEMORY;
        }
        paragraphs = (QwLayoutParagraph *)realloc(
            (void *)section->paragraphs, capacity * sizeof(*paragraphs));
        if (!paragraphs) return QW_EBOOK_ERR_OUT_OF_MEMORY;
        section->paragraphs = paragraphs;
        owner->paragraph_capacities[section_index] = capacity;
    }
    paragraphs = (QwLayoutParagraph *)section->paragraphs;
    paragraph = &paragraphs[section->paragraph_count++];
    memset(paragraph, 0, sizeof(*paragraph));
    paragraph->runs = builder->runs;
    paragraph->run_count = builder->run_count;
    paragraph->heading_level = heading_level;
    paragraph->page_break_before = section->paragraph_count == 1u &&
                                   owner->section_count > 1u;
    if (heading_level) {
        size_t i;
        for (i = 0; i < builder->run_count; ++i) {
            builder->runs[i].style_flags |= QW_LAYOUT_STYLE_HEADING;
        }
    } else if (flags & QW_LAYOUT_STYLE_QUOTE) {
        size_t i;
        for (i = 0; i < builder->run_count; ++i) {
            builder->runs[i].style_flags |= QW_LAYOUT_STYLE_QUOTE;
        }
    }
    ++owner->paragraph_count;
    memset(builder, 0, sizeof(*builder));
    return QW_EBOOK_OK;
}

static int epub_layout_collect_flow(QwEpubLayoutOwner *owner,
                                    QwLayoutSection *section, QwXmlNode *nodes,
                                    unsigned style_flags,
                                    QwEpubParagraphBuilder *pending,
                                    size_t depth)
{
    QwXmlNode *node;
    if (depth > 128u) return QW_EBOOK_ERR_UNSUPPORTED;
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (!tag) {
            const char *text = qw_xml_text(node);
            if (text && !epub_layout_run_append(owner, pending, text,
                                                style_flags, NULL)) {
                return QW_EBOOK_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        if (epub_layout_tag_is_block(tag)) {
            QwEpubParagraphBuilder paragraph = {0};
            unsigned level = epub_layout_heading_level(tag);
            unsigned flags = epub_layout_style_for_tag(tag, style_flags);
            int rc = epub_layout_flush_paragraph(owner, section, pending, 0u,
                                                  style_flags);
            if (rc != QW_EBOOK_OK) return rc;
            if (level) flags |= QW_LAYOUT_STYLE_HEADING;
            if (strcmp(epub_local_name(tag), "pre") == 0) {
                flags |= QW_LAYOUT_STYLE_CODE;
            }
            rc = epub_layout_collect_inline(owner, &paragraph,
                qw_xml_down(node), flags, NULL, depth + 1u);
            if (rc == QW_EBOOK_OK) {
                rc = epub_layout_flush_paragraph(owner, section, &paragraph,
                                                  level, flags);
            }
            epub_layout_paragraph_builder_dispose(&paragraph);
            if (rc != QW_EBOOK_OK) return rc;
            continue;
        }
        if (strcmp(epub_local_name(tag), "script") == 0 ||
            strcmp(epub_local_name(tag), "style") == 0) continue;
        if (strcmp(epub_local_name(tag), "br") == 0) {
            if (!epub_layout_run_append(owner, pending, " ", style_flags, NULL)) {
                return QW_EBOOK_ERR_OUT_OF_MEMORY;
            }
            continue;
        }
        {
            int rc = epub_layout_collect_flow(owner, section,
                qw_xml_down(node), epub_layout_style_for_tag(tag, style_flags),
                pending, depth + 1u);
            if (rc != QW_EBOOK_OK) return rc;
        }
    }
    return QW_EBOOK_OK;
}

static int epub_layout_requires_legacy(QwXmlNode *nodes, size_t depth)
{
    QwXmlNode *node;
    if (depth > 128u) return 1;
    for (node = nodes; node; node = qw_xml_next(node)) {
        const char *tag = qw_xml_tag(node);
        if (tag) {
            const char *name = epub_local_name(tag);
            if (qw_xml_att(node, "class") || qw_xml_att(node, "style") ||
                strcmp(name, "img") == 0 || strcmp(name, "picture") == 0 ||
                strcmp(name, "svg") == 0 || strcmp(name, "table") == 0 ||
                strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0 ||
                strcmp(name, "dl") == 0 || strcmp(name, "pre") == 0 ||
                strcmp(name, "canvas") == 0 || strcmp(name, "iframe") == 0 ||
                strcmp(name, "object") == 0 || strcmp(name, "video") == 0 ||
                strcmp(name, "audio") == 0 || strcmp(name, "math") == 0 ||
                strcmp(name, "style") == 0) return 1;
            if (strcmp(name, "link") == 0) {
                const char *rel = qw_xml_att(node, "rel");
                if (!rel || strstr(rel, "stylesheet")) return 1;
            }
        }
        if (epub_layout_requires_legacy(qw_xml_down(node), depth + 1u)) return 1;
    }
    return 0;
}


int qw_epub_native_open_layout(const void *data, size_t size,
                               QwInputFormat input_format,
                               const QwConvertOptions *options,
                               QwLayoutDocument *out_document,
                               QwConvertReport *report)
{
    QwZipArchive zip;
    QwEpubSpine spine = {0};
    QwEpubLayoutOwner *owner = NULL;
    QwXmlNode *container_xml = NULL;
    QwXmlNode *opf_xml = NULL;
    QwXmlNode *chapter_xml = NULL;
    void *container_data = NULL;
    void *opf_data = NULL;
    void *chapter_data = NULL;
    size_t container_size = 0u;
    size_t opf_size = 0u;
    size_t chapter_size = 0u;
    char *opf_path = NULL;
    size_t i;
    int archive_open = 0;
    int rc = QW_EBOOK_ERR_FORMAT;
    (void)options;
    if (!data || !size || !out_document || input_format != QW_FORMAT_EPUB) {
        return QW_EBOOK_ERR_INVALID_ARGUMENT;
    }
    memset(out_document, 0, sizeof(*out_document));
    memset(&zip, 0, sizeof(zip));
    if (!qw_zip_reader_init_mem_with_profile(
            &zip, data, size, 0u,
            QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE)) {
        return epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                              "EPUB package is not a supported ZIP archive");
    }
    archive_open = 1;
    container_data = epub_extract_named(&zip, "META-INF/container.xml",
                                        &container_size);
    if (!container_data || !container_size ||
        container_size > QW_EPUB_MAX_XML_SIZE) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB container.xml is missing or too large");
        goto cleanup;
    }
    container_xml = epub_parse_xml((const uint8_t *)container_data,
                                   container_size);
    if (!container_xml) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB container.xml could not be parsed");
        goto cleanup;
    }
    {
        QwXmlNode *rootfile = epub_find_descendant(qw_xml_root(container_xml),
                                                "rootfile", 0u);
        const char *full_path = rootfile ? qw_xml_att(rootfile, "full-path") : NULL;
        if (full_path) opf_path = epub_resolve_href("", full_path);
    }
    if (!opf_path) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB package path is missing or invalid");
        goto cleanup;
    }
    opf_data = epub_extract_named(&zip, opf_path, &opf_size);
    if (!opf_data || !opf_size || opf_size > QW_EPUB_MAX_XML_SIZE) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB OPF package is missing or too large");
        goto cleanup;
    }
    opf_xml = epub_parse_xml((const uint8_t *)opf_data, opf_size);
    if (!opf_xml || !epub_collect_spine(qw_xml_root(opf_xml), opf_path, &spine)) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB OPF manifest or spine is invalid");
        goto cleanup;
    }
    owner = (QwEpubLayoutOwner *)calloc(1u, sizeof(*owner));
    if (!owner) {
        rc = epub_set_error(report, QW_EBOOK_ERR_OUT_OF_MEMORY,
                            "Could not allocate EPUB layout document");
        goto cleanup;
    }
    for (i = 0; i < spine.count; ++i) {
        QwLayoutSection *section;
        QwEpubParagraphBuilder pending = {0};
        QwXmlNode *body;
        chapter_data = epub_extract_named(&zip, spine.items[i].path,
                                          &chapter_size);
        if (!chapter_data || !chapter_size) {
            rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                                "EPUB spine item is missing from the package");
            goto cleanup;
        }
        if (chapter_size > QW_EPUB_MAX_XML_SIZE) {
            rc = epub_set_error(report, QW_EBOOK_ERR_UNSUPPORTED,
                                "EPUB chapter exceeds the structured layout limit");
            goto cleanup;
        }
        chapter_xml = epub_parse_xml((const uint8_t *)chapter_data,
                                     chapter_size);
        if (!chapter_xml) {
            rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                                "EPUB spine XHTML could not be parsed");
            goto cleanup;
        }
        if (epub_layout_requires_legacy(qw_xml_root(chapter_xml), 0u)) {
            rc = epub_set_error(report, QW_EBOOK_ERR_UNSUPPORTED,
                                "EPUB chapter uses unsupported CSS or layout features; using the compatibility renderer");
            goto cleanup;
        }
        body = epub_find_descendant(qw_xml_root(chapter_xml), "body", 0u);
        if (!body) {
            rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                                "EPUB chapter has no XHTML body");
            goto cleanup;
        }
        section = epub_layout_section_append(owner);
        if (!section) {
            rc = epub_set_error(report, QW_EBOOK_ERR_OUT_OF_MEMORY,
                                "Could not allocate an EPUB chapter layout");
            goto cleanup;
        }
        rc = epub_layout_collect_flow(owner, section, qw_xml_down(body), 0u,
                                      &pending, 0u);
        if (rc == QW_EBOOK_OK) {
            rc = epub_layout_flush_paragraph(owner, section, &pending, 0u, 0u);
        }
        epub_layout_paragraph_builder_dispose(&pending);
        if (rc != QW_EBOOK_OK) {
            if (rc == QW_EBOOK_ERR_UNSUPPORTED) {
                epub_set_error(report, rc,
                    "EPUB chapter nesting exceeds the structured layout limit");
            } else {
                epub_set_error(report, rc,
                    "Could not allocate EPUB chapter text and runs");
            }
            goto cleanup;
        }
        if (section->paragraph_count == 0u) --owner->section_count;
        qw_xml_drop(chapter_xml);
        chapter_xml = NULL;
        free(chapter_data);
        chapter_data = NULL;
        chapter_size = 0u;
    }
    if (!owner->section_count) {
        rc = epub_set_error(report, QW_EBOOK_ERR_FORMAT,
                            "EPUB spine contains no readable text paragraphs");
        goto cleanup;
    }
    out_document->sections = owner->sections;
    out_document->section_count = owner->section_count;
    out_document->context = owner;
    out_document->dispose = epub_layout_owner_dispose;
    owner = NULL;
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
            "EPUB spine parsed into %lu structured chapter(s)",
            (unsigned long)out_document->section_count);
    }
    rc = QW_EBOOK_OK;

cleanup:
    free(chapter_data);
    qw_xml_drop(chapter_xml);
    if (owner) epub_layout_owner_dispose(owner);
    epub_spine_free(&spine);
    free(opf_path);
    free(opf_data);
    free(container_data);
    qw_xml_drop(opf_xml);
    qw_xml_drop(container_xml);
    if (archive_open) qw_zip_reader_end(&zip);
    return rc;
}

/* PDF emission is intentionally not implemented here: the EPUB library only
 * produces structured layout. Layout-to-PDF conversion belongs to the internal
 * layout/PDF renderer, which is not part of this package. */
int qw_epub_native_convert(const void *data, size_t size,
                           const QwConvertOptions *options,
                           uint8_t **out_pdf, size_t *out_size,
                           QwConvertReport *report)
{
    (void)data;
    (void)size;
    (void)options;
    if (out_pdf) *out_pdf = NULL;
    if (out_size) *out_size = 0;
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
                       "EPUB to PDF requires the internal Quirewise layout/PDF renderer, which is not available in this package");
    }
    return QW_EBOOK_ERR_UNSUPPORTED;
}
