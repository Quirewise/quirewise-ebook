#include "../../core/format_registry.h"
#include "fb2_format.h"

#ifndef QUIREWISE_STANDALONE_FB2_PARSER
#include "../../backends/mupdf_backend.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FB2_SNIFF_BYTES 16384u

typedef enum Fb2TextEncoding {
    FB2_TEXT_UTF8,
    FB2_TEXT_UTF16_LE,
    FB2_TEXT_UTF16_BE,
    FB2_TEXT_UTF32_LE,
    FB2_TEXT_UTF32_BE
} Fb2TextEncoding;

static int fb2_prefix_is(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int fb2_decode_markup_prefix(const uint8_t *data, size_t size,
                                   char *out, size_t out_capacity)
{
    Fb2TextEncoding encoding = FB2_TEXT_UTF8;
    size_t offset = 0;
    size_t unit_size = 1u;
    size_t written = 0;
    if (!data || !size || !out || out_capacity < 2u) return 0;
    if (size >= 4u && data[0] == 0xff && data[1] == 0xfe &&
        data[2] == 0 && data[3] == 0) {
        encoding = FB2_TEXT_UTF32_LE;
        offset = 4u;
        unit_size = 4u;
    } else if (size >= 4u && data[0] == 0 && data[1] == 0 &&
               data[2] == 0xfe && data[3] == 0xff) {
        encoding = FB2_TEXT_UTF32_BE;
        offset = 4u;
        unit_size = 4u;
    } else if (size >= 2u && data[0] == 0xff && data[1] == 0xfe) {
        encoding = FB2_TEXT_UTF16_LE;
        offset = 2u;
        unit_size = 2u;
    } else if (size >= 2u && data[0] == 0xfe && data[1] == 0xff) {
        encoding = FB2_TEXT_UTF16_BE;
        offset = 2u;
        unit_size = 2u;
    } else if (size >= 4u && data[0] == '<' && data[1] == 0 &&
               data[2] == '?' && data[3] == 0) {
        encoding = FB2_TEXT_UTF16_LE;
        unit_size = 2u;
    } else if (size >= 4u && data[0] == '<' && data[1] == 0 &&
               data[2] == 'F' && data[3] == 0) {
        encoding = FB2_TEXT_UTF16_LE;
        unit_size = 2u;
    } else if (size >= 4u && data[0] == 0 && data[1] == '<' &&
               data[2] == 0 && data[3] == '?') {
        encoding = FB2_TEXT_UTF16_BE;
        unit_size = 2u;
    } else if (size >= 4u && data[0] == 0 && data[1] == '<' &&
               data[2] == 0 && data[3] == 'F') {
        encoding = FB2_TEXT_UTF16_BE;
        unit_size = 2u;
    } else if (size >= 4u && data[0] == '<' && data[1] == 0 &&
               data[2] == 0 && data[3] == 0) {
        encoding = FB2_TEXT_UTF32_LE;
        unit_size = 4u;
    } else if (size >= 4u && data[0] == 0 && data[1] == 0 &&
               data[2] == 0 && data[3] == '<') {
        encoding = FB2_TEXT_UTF32_BE;
        unit_size = 4u;
    } else if (size >= 3u && data[0] == 0xef && data[1] == 0xbb &&
               data[2] == 0xbf) {
        offset = 3u;
    }

    while (offset + unit_size <= size && written + 1u < out_capacity &&
           written < FB2_SNIFF_BYTES) {
        uint32_t codepoint;
        if (encoding == FB2_TEXT_UTF8) {
            unsigned char ch = data[offset];
            if (ch == 0) break;
            out[written++] = (char)ch;
            ++offset;
            continue;
        }
        if (unit_size == 2u) {
            codepoint = encoding == FB2_TEXT_UTF16_LE
                ? (uint32_t)data[offset] | ((uint32_t)data[offset + 1u] << 8)
                : ((uint32_t)data[offset] << 8) | (uint32_t)data[offset + 1u];
        } else {
            codepoint = encoding == FB2_TEXT_UTF32_LE
                ? (uint32_t)data[offset] |
                    ((uint32_t)data[offset + 1u] << 8) |
                    ((uint32_t)data[offset + 2u] << 16) |
                    ((uint32_t)data[offset + 3u] << 24)
                : ((uint32_t)data[offset] << 24) |
                    ((uint32_t)data[offset + 1u] << 16) |
                    ((uint32_t)data[offset + 2u] << 8) |
                    (uint32_t)data[offset + 3u];
        }
        if (codepoint == 0) break;
        out[written++] = codepoint < 0x80u ? (char)codepoint : '?';
        offset += unit_size;
    }
    out[written] = '\0';
    return written != 0;
}

static const char *fb2_skip_doctype(const char *cursor)
{
    size_t bracket_depth = 0;
    char quote = '\0';
    if (!fb2_prefix_is(cursor, "<!DOCTYPE")) return NULL;
    cursor += strlen("<!DOCTYPE");
    while (*cursor) {
        if (quote) {
            if (*cursor == quote) quote = '\0';
        } else if (*cursor == '\'' || *cursor == '"') {
            quote = *cursor;
        } else if (*cursor == '[') {
            ++bracket_depth;
        } else if (*cursor == ']' && bracket_depth) {
            --bracket_depth;
        } else if (*cursor == '>' && bracket_depth == 0u) {
            return cursor + 1u;
        }
        ++cursor;
    }
    return NULL;
}

static int fb2_is_root_tag(const char *cursor)
{
    const char *name;
    const char *local_name;
    const char *end;
    if (*cursor != '<' || cursor[1] == '/' || cursor[1] == '!' ||
        cursor[1] == '?') return 0;
    name = cursor + 1u;
    end = name;
    local_name = name;
    while ((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') ||
           (*end >= '0' && *end <= '9') || *end == '_' || *end == '-' ||
           *end == ':' || *end == '.') {
        if (*end == ':') local_name = end + 1u;
        ++end;
    }
    return end > local_name &&
           (size_t)(end - local_name) == sizeof("FictionBook") - 1u &&
           memcmp(local_name, "FictionBook", sizeof("FictionBook") - 1u) == 0 &&
           (*end == '>' || *end == '/' || *end == ' ' || *end == '\t' ||
            *end == '\r' || *end == '\n');
}

static int fb2_probe(const void *data, size_t size)
{
    char prefix[FB2_SNIFF_BYTES + 1u];
    const char *cursor;
    if (!fb2_decode_markup_prefix((const uint8_t *)data, size, prefix,
                                  sizeof(prefix))) return 0;
    cursor = prefix;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    while (*cursor) {
        if (fb2_prefix_is(cursor, "<?")) {
            const char *end = strstr(cursor + 2u, "?>");
            if (!end) return 0;
            cursor = end + 2u;
        } else if (fb2_prefix_is(cursor, "<!--")) {
            const char *end = strstr(cursor + 4u, "-->");
            if (!end) return 0;
            cursor = end + 3u;
        } else if (fb2_prefix_is(cursor, "<!DOCTYPE")) {
            cursor = fb2_skip_doctype(cursor);
            if (!cursor) return 0;
        } else {
            return fb2_is_root_tag(cursor);
        }
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
    }
    return 0;
}

int qw_fb2_probe_native(const void *data, size_t size)
{
    return fb2_probe(data, size);
}

#ifndef QUIREWISE_STANDALONE_FB2_PARSER
static int fb2_convert_to_pdf(const void *data, size_t size,
                              const QwConvertOptions *options,
                              uint8_t **out_pdf, size_t *out_size,
                              QwConvertReport *report)
{
    QwLayoutDocument layout;
    int rc;
    if (!out_pdf || !out_size) return QW_EBOOK_ERR_INVALID_ARGUMENT;
    *out_pdf = NULL;
    *out_size = 0u;
    memset(&layout, 0, sizeof(layout));
    rc = qw_fb2_native_open_layout(data, size, QW_FORMAT_FB2, options,
                                   &layout, report);
    if (rc != QW_EBOOK_OK) goto fb2_done;
    if (!layout.html_utf8 || !layout.html_size) {
        rc = QW_EBOOK_ERR_FORMAT;
        if (report) snprintf(report->note, sizeof(report->note),
                             "FictionBook parser returned no HTML layout");
        goto fb2_done;
    }
    rc = qw_mupdf_convert_html((const char *)layout.html_utf8,
                               layout.html_size, out_pdf, out_size, report);
fb2_done:
    if (layout.dispose && layout.context) layout.dispose(layout.context);
    return rc;
}









const QwFormatDescriptor qw_format_fb2 = {
    .id = QW_FORMAT_FB2,
    .name = "FictionBook 2",
    .capabilities = (QW_FORMAT_CAP_MULTI_PAGE) | (0u),
    .probe = fb2_probe,
    .open_memory = NULL,
    .open_layout = qw_fb2_native_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = fb2_convert_to_pdf
};
#endif
