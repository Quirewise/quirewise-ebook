#include "mobi_internal.h"

#include <stdlib.h>
#include <string.h>

enum { MOBI_MARKUP_MAX_OUTPUT = 128u * 1024u * 1024u };

typedef struct MobiTextBuffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int error;
} MobiTextBuffer;

typedef struct MobiTag {
    size_t name_start;
    size_t name_size;
    size_t after;
    int closing;
    int declaration;
} MobiTag;

static int mobi_markup_append(MobiTextBuffer *buffer,
                              const uint8_t *data, size_t size)
{
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (!buffer || (!data && size)) return 0;
    if (size > (size_t)MOBI_MARKUP_MAX_OUTPUT - buffer->size ||
        buffer->size + size == SIZE_MAX) {
        buffer->error = QW_EBOOK_ERR_RESOURCE_LIMIT;
        return 0;
    }
    required = buffer->size + size + 1u;
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 4096u;
        while (capacity < required) {
            if (capacity > (size_t)MOBI_MARKUP_MAX_OUTPUT / 2u) {
                capacity = (size_t)MOBI_MARKUP_MAX_OUTPUT + 1u;
                break;
            }
            capacity *= 2u;
        }
        if (capacity < required) {
            buffer->error = QW_EBOOK_ERR_RESOURCE_LIMIT;
            return 0;
        }
        grown = (uint8_t *)realloc(buffer->data, capacity);
        if (!grown) {
            buffer->error = QW_EBOOK_ERR_OUT_OF_MEMORY;
            return 0;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (size) memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = 0u;
    return 1;
}

static int mobi_markup_append_codepoint(MobiTextBuffer *buffer,
                                        uint32_t codepoint)
{
    uint8_t encoded[4];
    size_t size;
    if (codepoint == 0u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = 0xfffdu;
    }
    if (codepoint < 0x80u) {
        encoded[0] = (uint8_t)codepoint;
        size = 1u;
    } else if (codepoint < 0x800u) {
        encoded[0] = (uint8_t)(0xc0u | (codepoint >> 6));
        encoded[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        size = 2u;
    } else if (codepoint < 0x10000u) {
        encoded[0] = (uint8_t)(0xe0u | (codepoint >> 12));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        size = 3u;
    } else {
        encoded[0] = (uint8_t)(0xf0u | (codepoint >> 18));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 12) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        size = 4u;
    }
    return mobi_markup_append(buffer, encoded, size);
}

static int mobi_markup_ascii_equal(const uint8_t *data, size_t size,
                                   const char *literal)
{
    size_t literal_size = strlen(literal);
    if (size != literal_size) return 0;
    for (size_t i = 0u; i < size; ++i) {
        uint8_t value = data[i];
        if (value >= (uint8_t)'A' && value <= (uint8_t)'Z')
            value = (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'));
        if (value != (uint8_t)literal[i]) return 0;
    }
    return 1;
}

static int mobi_markup_name_char(uint8_t value)
{
    return (value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
           (value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
           (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
           value == (uint8_t)':' || value == (uint8_t)'-' ||
           value == (uint8_t)'_';
}

static int mobi_markup_parse_tag(const uint8_t *data, size_t size,
                                 size_t start, MobiTag *out_tag)
{
    size_t position;
    uint8_t quote = 0u;
    if (!data || !out_tag || start >= size || data[start] != (uint8_t)'<')
        return 0;
    position = start + 1u;
    if (position >= size) return 0;
    memset(out_tag, 0, sizeof(*out_tag));
    if (data[position] == (uint8_t)'!' || data[position] == (uint8_t)'?') {
        out_tag->declaration = 1;
        for (; position < size; ++position) {
            uint8_t value = data[position];
            if (quote) {
                if (value == quote) quote = 0u;
            } else if (value == (uint8_t)'\'' || value == (uint8_t)'"') {
                quote = value;
            } else if (value == (uint8_t)'>') {
                out_tag->after = position + 1u;
                return 1;
            }
        }
        out_tag->after = size;
        return 1;
    }
    if (data[position] == (uint8_t)'/') {
        out_tag->closing = 1;
        ++position;
    }
    out_tag->name_start = position;
    while (position < size && mobi_markup_name_char(data[position]))
        ++position;
    out_tag->name_size = position - out_tag->name_start;
    if (!out_tag->name_size) return 0;
    if (position < size && data[position] != (uint8_t)' ' &&
        data[position] != (uint8_t)'\t' && data[position] != (uint8_t)'\r' &&
        data[position] != (uint8_t)'\n' && data[position] != (uint8_t)'/' &&
        data[position] != (uint8_t)'>') return 0;
    for (; position < size; ++position) {
        uint8_t value = data[position];
        if (quote) {
            if (value == quote) quote = 0u;
        } else if (value == (uint8_t)'\'' || value == (uint8_t)'"') {
            quote = value;
        } else if (value == (uint8_t)'<') {
            return 0;
        } else if (value == (uint8_t)'>') {
            out_tag->after = position + 1u;
            return 1;
        }
    }
    return 0;
}

static int mobi_markup_contains_tags(const uint8_t *data, size_t size)
{
    for (size_t position = 0u; position < size; ++position) {
        MobiTag tag;
        if (data[position] != (uint8_t)'<') continue;
        if (position + 3u < size && data[position + 1u] == (uint8_t)'!' &&
            data[position + 2u] == (uint8_t)'-' &&
            data[position + 3u] == (uint8_t)'-') return 1;
        if (mobi_markup_parse_tag(data, size, position, &tag)) return 1;
    }
    return 0;
}

static int mobi_markup_is_block_tag(const uint8_t *name, size_t size)
{
    static const char *const block_tags[] = {
        "address", "article", "blockquote", "body", "dd", "div", "dl",
        "dt", "h1", "h2", "h3", "h4", "h5", "h6", "header", "li",
        "ol", "p", "pre", "section", "table", "tr", "ul"
    };
    for (size_t i = 0u; i < sizeof(block_tags) / sizeof(block_tags[0]); ++i) {
        if (mobi_markup_ascii_equal(name, size, block_tags[i])) return 1;
    }
    return 0;
}

static int mobi_markup_append_break(MobiTextBuffer *buffer, size_t count)
{
    static const uint8_t newline = (uint8_t)'\n';
    size_t trailing_newlines = 0u;
    while (buffer->size &&
           (buffer->data[buffer->size - 1u] == (uint8_t)' ' ||
            buffer->data[buffer->size - 1u] == (uint8_t)'\t' ||
            buffer->data[buffer->size - 1u] == (uint8_t)'\r')) {
        --buffer->size;
    }
    while (trailing_newlines < buffer->size &&
           buffer->data[buffer->size - trailing_newlines - 1u] == newline) {
        ++trailing_newlines;
    }
    while (trailing_newlines < count) {
        if (!mobi_markup_append(buffer, &newline, 1u)) return 0;
        ++trailing_newlines;
    }
    return 1;
}

static int mobi_markup_named_entity(const uint8_t *name, size_t size,
                                    uint32_t *out_codepoint)
{
    static const struct {
        const char *name;
        uint32_t codepoint;
    } entities[] = {
        {"amp", 0x26u}, {"apos", 0x27u}, {"bull", 0x2022u},
        {"cent", 0xa2u}, {"copy", 0xa9u}, {"deg", 0xb0u},
        {"divide", 0xf7u}, {"euro", 0x20acu}, {"hellip", 0x2026u},
        {"laquo", 0xabu}, {"ldquo", 0x201cu}, {"lsquo", 0x2018u},
        {"lt", 0x3cu}, {"mdash", 0x2014u}, {"middot", 0xb7u},
        {"nbsp", 0xa0u}, {"ndash", 0x2013u}, {"para", 0xb6u},
        {"pound", 0xa3u}, {"quot", 0x22u}, {"raquo", 0xbbu},
        {"rdquo", 0x201du}, {"reg", 0xaeu}, {"rsquo", 0x2019u},
        {"sect", 0xa7u}, {"trade", 0x2122u}, {"yen", 0xa5u}
    };
    for (size_t i = 0u; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        if (mobi_markup_ascii_equal(name, size, entities[i].name)) {
            *out_codepoint = entities[i].codepoint;
            return 1;
        }
    }
    return 0;
}

static int mobi_markup_parse_entity(const uint8_t *data, size_t size,
                                    size_t start, size_t *out_after,
                                    uint32_t *out_codepoint)
{
    size_t end = start + 1u;
    uint32_t codepoint = 0u;
    if (start >= size || data[start] != (uint8_t)'&') return 0;
    while (end < size && end - start <= 16u &&
           data[end] != (uint8_t)';' && data[end] != (uint8_t)' ' &&
           data[end] != (uint8_t)'\t' && data[end] != (uint8_t)'\r' &&
           data[end] != (uint8_t)'\n' && data[end] != (uint8_t)'<') {
        ++end;
    }
    if (end >= size || data[end] != (uint8_t)';' || end == start + 1u)
        return 0;
    if (data[start + 1u] == (uint8_t)'#') {
        size_t digit = start + 2u;
        unsigned base = 10u;
        if (digit < end && (data[digit] == (uint8_t)'x' ||
                            data[digit] == (uint8_t)'X')) {
            base = 16u;
            ++digit;
        }
        if (digit == end) return 0;
        for (; digit < end; ++digit) {
            uint8_t value = data[digit];
            unsigned number;
            if (value >= (uint8_t)'0' && value <= (uint8_t)'9') {
                number = (unsigned)(value - (uint8_t)'0');
            } else if (base == 16u && value >= (uint8_t)'a' &&
                       value <= (uint8_t)'f') {
                number = (unsigned)(value - (uint8_t)'a') + 10u;
            } else if (base == 16u && value >= (uint8_t)'A' &&
                       value <= (uint8_t)'F') {
                number = (unsigned)(value - (uint8_t)'A') + 10u;
            } else {
                return 0;
            }
            if (number >= base || codepoint > (0x10ffffu - number) / base)
                return 0;
            codepoint = codepoint * base + number;
        }
        if (codepoint == 0u || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            codepoint = 0xfffdu;
        }
    } else if (!mobi_markup_named_entity(data + start + 1u,
                                          end - start - 1u, &codepoint)) {
        return 0;
    }
    *out_after = end + 1u;
    *out_codepoint = codepoint;
    return 1;
}

static int mobi_markup_append_text(MobiTextBuffer *buffer,
                                   const uint8_t *data, size_t size,
                                   size_t *out_consumed)
{
    size_t position = 0u;
    while (position < size) {
        size_t after;
        uint32_t codepoint;
        if (data[position] == 0u) {
            buffer->error = QW_EBOOK_ERR_DECODE;
            return 0;
        }
        if (data[position] == (uint8_t)'&' &&
            mobi_markup_parse_entity(data, size, position, &after,
                                     &codepoint)) {
            if (!mobi_markup_append_codepoint(buffer, codepoint)) return 0;
            position = after;
        } else if (data[position] == (uint8_t)'\r') {
            static const uint8_t newline = (uint8_t)'\n';
            if (!mobi_markup_append(buffer, &newline, 1u)) return 0;
            ++position;
            if (position < size && data[position] == (uint8_t)'\n') ++position;
        } else if (!mobi_markup_append(buffer, data + position, 1u)) {
            return 0;
        } else {
            ++position;
        }
    }
    if (out_consumed) *out_consumed = position;
    return 1;
}

int qw_mobi_markup_to_text(const uint8_t *markup, size_t markup_size,
                           uint8_t **out_text, size_t *out_text_size)
{
    MobiTextBuffer output = {0};
    size_t position = 0u;
    int suppress_script = 0;
    int suppress_style = 0;
    int rc = QW_EBOOK_OK;
    if ((!markup && markup_size) || !out_text || !out_text_size)
        return QW_EBOOK_ERR_INVALID_ARGUMENT;
    *out_text = NULL;
    *out_text_size = 0u;
    if (!mobi_markup_contains_tags(markup, markup_size)) {
        if (!markup_size) return QW_EBOOK_ERR_UNSUPPORTED;
        if (markup_size == SIZE_MAX) return QW_EBOOK_ERR_RESOURCE_LIMIT;
        for (size_t i = 0u; i < markup_size; ++i) {
            if (markup[i] == 0u) return QW_EBOOK_ERR_DECODE;
        }
        output.data = (uint8_t *)malloc(markup_size + 1u);
        if (!output.data) return QW_EBOOK_ERR_OUT_OF_MEMORY;
        memcpy(output.data, markup, markup_size);
        output.data[markup_size] = 0u;
        *out_text = output.data;
        *out_text_size = markup_size;
        return QW_EBOOK_OK;
    }
    while (position < markup_size) {
        MobiTag tag;
        if (markup[position] == (uint8_t)'<' &&
            position + 3u < markup_size &&
            markup[position + 1u] == (uint8_t)'!' &&
            markup[position + 2u] == (uint8_t)'-' &&
            markup[position + 3u] == (uint8_t)'-') {
            size_t comment_end = position + 4u;
            while (comment_end + 2u < markup_size &&
                   !(markup[comment_end] == (uint8_t)'-' &&
                     markup[comment_end + 1u] == (uint8_t)'-' &&
                     markup[comment_end + 2u] == (uint8_t)'>')) {
                ++comment_end;
            }
            position = comment_end + 2u < markup_size
                ? comment_end + 3u : markup_size;
            continue;
        }
        if (markup[position] == (uint8_t)'<' &&
            mobi_markup_parse_tag(markup, markup_size, position, &tag)) {
            const uint8_t *tag_name = markup + tag.name_start;
            if (suppress_script || suppress_style) {
                if (tag.closing && suppress_script &&
                    mobi_markup_ascii_equal(tag_name, tag.name_size,
                                            "script")) {
                    suppress_script = 0;
                }
                if (tag.closing && suppress_style &&
                    mobi_markup_ascii_equal(tag_name, tag.name_size,
                                            "style")) {
                    suppress_style = 0;
                }
            } else if (!tag.declaration && !tag.closing &&
                mobi_markup_ascii_equal(tag_name, tag.name_size, "script")) {
                suppress_script = 1;
            } else if (!tag.declaration && !tag.closing &&
                       mobi_markup_ascii_equal(tag_name, tag.name_size,
                                               "style")) {
                suppress_style = 1;
            } else if (!suppress_script && !suppress_style &&
                       !tag.declaration) {
                if (mobi_markup_ascii_equal(tag_name, tag.name_size, "br") ||
                    mobi_markup_ascii_equal(tag_name, tag.name_size, "hr") ||
                    mobi_markup_ascii_equal(tag_name, tag.name_size,
                                            "mbp:pagebreak")) {
                    size_t breaks = mobi_markup_ascii_equal(
                        tag_name, tag.name_size, "br") ? 1u : 2u;
                    if (!mobi_markup_append_break(&output, breaks)) goto fail;
                } else if (mobi_markup_is_block_tag(tag_name, tag.name_size)) {
                    size_t breaks = mobi_markup_ascii_equal(
                        tag_name, tag.name_size, "p") ||
                        (tag.name_size == 2u && tag_name[0] == (uint8_t)'h')
                            ? 2u : 1u;
                    if (!mobi_markup_append_break(&output, breaks)) goto fail;
                    if (!tag.closing && mobi_markup_ascii_equal(
                            tag_name, tag.name_size, "li") &&
                        (!mobi_markup_append_codepoint(&output, 0x2022u) ||
                         !mobi_markup_append(&output,
                                             (const uint8_t *)" ", 1u))) {
                        goto fail;
                    }
                }
            }
            position = tag.after;
            continue;
        }
        if (suppress_script || suppress_style) {
            ++position;
            continue;
        }
        {
            size_t end = position + 1u;
            while (end < markup_size && markup[end] != (uint8_t)'<' &&
                   markup[end] != (uint8_t)'\r') {
                ++end;
            }
            if (!mobi_markup_append_text(&output, markup + position,
                                         end - position, NULL)) goto fail;
            position = end;
        }
    }
    while (output.size &&
           (output.data[output.size - 1u] == (uint8_t)' ' ||
            output.data[output.size - 1u] == (uint8_t)'\t' ||
            output.data[output.size - 1u] == (uint8_t)'\r' ||
            output.data[output.size - 1u] == (uint8_t)'\n')) {
        --output.size;
    }
    {
        size_t leading = 0u;
        while (leading < output.size &&
               (output.data[leading] == (uint8_t)' ' ||
                output.data[leading] == (uint8_t)'\t' ||
                output.data[leading] == (uint8_t)'\r' ||
                output.data[leading] == (uint8_t)'\n')) {
            ++leading;
        }
        if (leading) {
            memmove(output.data, output.data + leading,
                    output.size - leading);
            output.size -= leading;
        }
    }
    if (!output.size) {
        rc = QW_EBOOK_ERR_UNSUPPORTED;
        goto done;
    }
    output.data[output.size] = 0u;
    *out_text = output.data;
    *out_text_size = output.size;
    output.data = NULL;
    goto done;
fail:
    rc = output.error ? output.error : QW_EBOOK_ERR_OUT_OF_MEMORY;
done:
    free(output.data);
    return rc;
}
