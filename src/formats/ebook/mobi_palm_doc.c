#include "mobi_internal.h"

#include <stdlib.h>
#include <string.h>

enum {
    MOBI_PDB_HEADER_SIZE = 78u,
    MOBI_PDB_RECORD_ENTRY_SIZE = 8u,
    MOBI_PALMDOC_HEADER_SIZE = 16u,
    MOBI_NATIVE_MAX_TEXT_SIZE = 32u * 1024u * 1024u
};

static uint16_t mobi_read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t mobi_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static int mobi_validate_pdb_records(const uint8_t *data, size_t size,
                                     size_t *out_count, size_t *out_table_end)
{
    size_t count;
    size_t table_end;
    uint32_t previous = 0u;
    if (!data || size < MOBI_PDB_HEADER_SIZE) return 0;
    count = mobi_read_u16_be(data + 76u);
    if (!count || count > (SIZE_MAX - MOBI_PDB_HEADER_SIZE) /
                          MOBI_PDB_RECORD_ENTRY_SIZE) return 0;
    table_end = MOBI_PDB_HEADER_SIZE + count * MOBI_PDB_RECORD_ENTRY_SIZE;
    if (table_end > size) return 0;
    for (size_t i = 0u; i < count; ++i) {
        size_t entry_offset = MOBI_PDB_HEADER_SIZE +
                              i * MOBI_PDB_RECORD_ENTRY_SIZE;
        uint32_t record_offset = mobi_read_u32_be(data + entry_offset);
        if ((size_t)record_offset < table_end ||
            (size_t)record_offset >= size ||
            (i && record_offset <= previous)) return 0;
        previous = record_offset;
    }
    *out_count = count;
    *out_table_end = table_end;
    return 1;
}

static int mobi_utf8_valid(const uint8_t *data, size_t size)
{
    size_t position = 0u;
    while (position < size) {
        uint8_t first = data[position++];
        uint32_t codepoint;
        unsigned continuation_count;
        uint32_t minimum;
        if (first < 0x80u) {
            if (!first) return 0;
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            continuation_count = 1u;
            minimum = 0x80u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            continuation_count = 2u;
            minimum = 0x800u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            continuation_count = 3u;
            minimum = 0x10000u;
        } else {
            return 0;
        }
        if (continuation_count > size - position) return 0;
        for (unsigned i = 0u; i < continuation_count; ++i) {
            uint8_t next = data[position++];
            if ((next & 0xc0u) != 0x80u) return 0;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return 0;
    }
    return 1;
}

static size_t mobi_utf8_encode(uint32_t codepoint, uint8_t output[4])
{
    if (codepoint < 0x80u) {
        output[0] = (uint8_t)codepoint;
        return 1u;
    }
    if (codepoint < 0x800u) {
        output[0] = (uint8_t)(0xc0u | (codepoint >> 6));
        output[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        return 2u;
    }
    if (codepoint < 0x10000u) {
        output[0] = (uint8_t)(0xe0u | (codepoint >> 12));
        output[1] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
        output[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        return 3u;
    }
    output[0] = (uint8_t)(0xf0u | (codepoint >> 18));
    output[1] = (uint8_t)(0x80u | ((codepoint >> 12) & 0x3fu));
    output[2] = (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu));
    output[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
    return 4u;
}

static int mobi_decode_cp1252(const uint8_t *input, size_t input_size,
                              uint8_t **out_utf8, size_t *out_size)
{
    static const uint16_t cp1252_controls[32] = {
        0x20acu, 0x0081u, 0x201au, 0x0192u, 0x201eu, 0x2026u, 0x2020u,
        0x2021u, 0x02c6u, 0x2030u, 0x0160u, 0x2039u, 0x0152u, 0x008du,
        0x017du, 0x008fu, 0x0090u, 0x2018u, 0x2019u, 0x201cu, 0x201du,
        0x2022u, 0x2013u, 0x2014u, 0x02dcu, 0x2122u, 0x0161u, 0x203au,
        0x0153u, 0x009du, 0x017eu, 0x0178u
    };
    uint8_t *utf8;
    size_t capacity;
    size_t position = 0u;
    if (input_size > SIZE_MAX / 3u) return 0;
    capacity = input_size * 3u + 1u;
    utf8 = (uint8_t *)malloc(capacity);
    if (!utf8) return 0;
    for (size_t i = 0u; i < input_size; ++i) {
        uint8_t encoded[4];
        uint32_t codepoint = input[i];
        size_t encoded_size;
        if (codepoint >= 0x80u && codepoint <= 0x9fu)
            codepoint = cp1252_controls[codepoint - 0x80u];
        encoded_size = mobi_utf8_encode(codepoint, encoded);
        memcpy(utf8 + position, encoded, encoded_size);
        position += encoded_size;
    }
    utf8[position] = 0u;
    *out_utf8 = utf8;
    *out_size = position;
    return 1;
}

int qw_mobi_parse_palm_doc(const void *input, size_t size,
                           QwMobiBook *out_book)
{
    const uint8_t *data = (const uint8_t *)input;
    size_t record_count;
    size_t table_end;
    size_t record0_offset;
    size_t record0_end;
    size_t record0_size;
    const uint8_t *record0;
    const uint8_t *mobi_header;
    uint32_t mobi_header_size;
    uint32_t version;
    uint32_t encoding;
    uint32_t text_length;
    uint16_t compression;
    uint16_t text_record_count;
    uint16_t record_size;
    uint16_t encryption_type;
    QwInputFormat input_format;
    uint8_t *text = NULL;
    uint8_t *utf8 = NULL;
    uint8_t *plain_text = NULL;
    size_t text_position = 0u;
    size_t utf8_size = 0u;
    size_t plain_text_size = 0u;
    int rc = QW_EBOOK_ERR_FORMAT;
    if (!out_book) return QW_EBOOK_ERR_INVALID_ARGUMENT;
    *out_book = (QwMobiBook){0};
    input_format = qw_mobi_detect_type(input, size);
    if (input_format != QW_FORMAT_MOBI && input_format != QW_FORMAT_AZW3)
        return QW_EBOOK_ERR_UNSUPPORTED;
    if (!mobi_validate_pdb_records(data, size, &record_count, &table_end))
        return QW_EBOOK_ERR_FORMAT;
    record0_offset = mobi_read_u32_be(data + MOBI_PDB_HEADER_SIZE);
    record0_end = record_count > 1u
        ? mobi_read_u32_be(data + MOBI_PDB_HEADER_SIZE +
                           MOBI_PDB_RECORD_ENTRY_SIZE)
        : size;
    if (record0_end <= record0_offset || record0_end > size ||
        record0_offset < table_end) return QW_EBOOK_ERR_FORMAT;
    record0_size = record0_end - record0_offset;
    if (record0_size < MOBI_PALMDOC_HEADER_SIZE + 24u)
        return QW_EBOOK_ERR_FORMAT;
    record0 = data + record0_offset;
    mobi_header = record0 + MOBI_PALMDOC_HEADER_SIZE;
    if (memcmp(mobi_header, "MOBI", 4u) != 0) return QW_EBOOK_ERR_FORMAT;
    mobi_header_size = mobi_read_u32_be(mobi_header + 4u);
    if (mobi_header_size < 24u ||
        mobi_header_size > record0_size - MOBI_PALMDOC_HEADER_SIZE)
        return QW_EBOOK_ERR_FORMAT;
    version = mobi_read_u32_be(mobi_header + 20u);
    compression = mobi_read_u16_be(record0);
    text_length = mobi_read_u32_be(record0 + 4u);
    text_record_count = mobi_read_u16_be(record0 + 8u);
    record_size = mobi_read_u16_be(record0 + 10u);
    encryption_type = mobi_read_u16_be(record0 + 12u);
    encoding = mobi_read_u32_be(mobi_header + 12u);
    if (encryption_type != 0u || (compression != 1u && compression != 2u) ||
        version > 8u ||
        !record_size ||
        text_record_count == 0u || text_record_count >= record_count ||
        text_length > MOBI_NATIVE_MAX_TEXT_SIZE || !text_length ||
        (encoding != 1252u && encoding != 65001u)) {
        return QW_EBOOK_ERR_UNSUPPORTED;
    }
    text = (uint8_t *)malloc((size_t)text_length);
    if (!text) return QW_EBOOK_ERR_OUT_OF_MEMORY;
    for (size_t record_index = 1u;
         record_index <= (size_t)text_record_count; ++record_index) {
        size_t entry_offset = MOBI_PDB_HEADER_SIZE +
                              record_index * MOBI_PDB_RECORD_ENTRY_SIZE;
        size_t start = mobi_read_u32_be(data + entry_offset);
        size_t end = record_index + 1u < record_count
            ? mobi_read_u32_be(data + entry_offset +
                               MOBI_PDB_RECORD_ENTRY_SIZE)
            : size;
        size_t capacity = (size_t)text_length - text_position;
        size_t decoded_size = 0u;
        if (end < start || end > size) goto done;
        if (capacity > record_size) capacity = record_size;
        if (compression == 1u) {
            size_t packed_size = end - start;
            if (packed_size > capacity) goto done;
            memcpy(text + text_position, data + start, packed_size);
            decoded_size = packed_size;
        } else if (!qw_palm_doc_decode_record(data + start, end - start,
                                              text, text_position,
                                              text_position + capacity,
                                              &decoded_size)) {
            goto done;
        }
        text_position += decoded_size;
        if (text_position > (size_t)text_length) goto done;
    }
    if (text_position != (size_t)text_length) goto done;
    if (encoding == 65001u) {
        if (!mobi_utf8_valid(text, text_position)) goto done;
        utf8 = (uint8_t *)malloc(text_position + 1u);
        if (!utf8) {
            rc = QW_EBOOK_ERR_OUT_OF_MEMORY;
            goto done;
        }
        memcpy(utf8, text, text_position);
        utf8[text_position] = 0u;
        utf8_size = text_position;
    } else if (!mobi_decode_cp1252(text, text_position, &utf8, &utf8_size)) {
        rc = QW_EBOOK_ERR_OUT_OF_MEMORY;
        goto done;
    }
    rc = qw_mobi_markup_to_text(utf8, utf8_size,
                                &plain_text, &plain_text_size);
    if (rc != QW_EBOOK_OK) goto done;
    out_book->text = plain_text;
    out_book->text_size = plain_text_size;
    plain_text = NULL;
    rc = QW_EBOOK_OK;
done:
    free(plain_text);
    free(utf8);
    free(text);
    return rc;
}

void qw_mobi_book_drop(QwMobiBook *book)
{
    if (!book) return;
    free(book->text);
    *book = (QwMobiBook){0};
}
