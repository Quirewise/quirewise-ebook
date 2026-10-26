#include "html_probe.h"
#include "../../core/byte_utils.h"

#include <stdint.h>

enum { QW_HTML_SNIFF_LIMIT = 65536 };


static int html_match_at(const uint8_t *data, size_t size, size_t offset,
                         const char *text, size_t text_size,
                         int unit_size, int big_endian)
{
    size_t bytes_needed;
    if (text_size > SIZE_MAX / (size_t)unit_size) return 0;
    bytes_needed = text_size * (size_t)unit_size;
    if (offset > size || bytes_needed > size - offset) return 0;
    for (size_t i = 0; i < text_size; ++i) {
        unsigned char current;
        if (unit_size == 1) {
            current = data[offset + i];
        } else {
            size_t at = offset + i * 2u;
            unsigned char high = data[at + (big_endian ? 0u : 1u)];
            if (high != 0u) return 0;
            current = data[at + (big_endian ? 1u : 0u)];
        }
        if (qw_ascii_lower(current) != qw_ascii_lower((unsigned char)text[i])) return 0;
    }
    return 1;
}

static int html_tag_boundary(const uint8_t *data, size_t size, size_t offset,
                             size_t tag_size, int unit_size, int big_endian)
{
    size_t at = offset + tag_size * (size_t)unit_size;
    unsigned char next;
    if (at >= size || (size_t)unit_size > size - at) return 0;
    if (unit_size == 1) {
        next = data[at];
    } else {
        unsigned char high = data[at + (big_endian ? 0u : 1u)];
        if (high != 0u) return 0;
        next = data[at + (big_endian ? 1u : 0u)];
    }
    return next == '>' || next == '/' || next == ' ' || next == '\t' ||
           next == '\r' || next == '\n';
}

static int html_contains_root(const uint8_t *data, size_t size,
                              int unit_size, int big_endian)
{
    static const char doctype[] = "<!doctype html";
    static const char root[] = "<html";
    size_t step = (size_t)unit_size;
    for (size_t offset = 0; offset + sizeof(root) - 1u <= size;
         offset += step) {
        if (html_match_at(data, size, offset, doctype,
                          sizeof(doctype) - 1u, unit_size, big_endian) &&
            html_tag_boundary(data, size, offset,
                              sizeof(doctype) - 1u, unit_size, big_endian)) return 1;
        if (html_match_at(data, size, offset, root,
                          sizeof(root) - 1u, unit_size, big_endian) &&
            html_tag_boundary(data, size, offset,
                              sizeof(root) - 1u, unit_size, big_endian)) return 1;
    }
    return 0;
}

int qw_html_format_probe(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t scan_size;
    int unit_size = 1;
    int big_endian = 0;
    size_t offset = 0;
    if (!bytes || size < 5u) return 0;
    scan_size = size < QW_HTML_SNIFF_LIMIT ? size : QW_HTML_SNIFF_LIMIT;
    if (size >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu) {
        offset = 3u;
    } else if (size >= 2u && bytes[0] == 0xffu && bytes[1] == 0xfeu) {
        unit_size = 2;
        offset = 2u;
    } else if (size >= 2u && bytes[0] == 0xfeu && bytes[1] == 0xffu) {
        unit_size = 2;
        big_endian = 1;
        offset = 2u;
    }
    return html_contains_root(bytes + offset, scan_size - offset,
                              unit_size, big_endian);
}
