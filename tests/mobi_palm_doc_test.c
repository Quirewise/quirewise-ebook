/*
 * PalmDOC regression: a back-reference in a later text record may point into
 * the bytes produced by an earlier record, and records may carry trailing pad
 * bytes after the decoded record is full. Both cases used to fail with
 * QW_MOBI_ERROR_FORMAT.
 */

#include <quirewise/mobi.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

static void put_be16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static void put_be32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

int main(void)
{
    enum {
        HEADER_SIZE = 78,
        RECORD0 = 102, /* 78-byte PDB header + 3 record entries */
        RECORD0_SIZE = 16 + 232,
        RECORD1 = RECORD0 + RECORD0_SIZE,
        RECORD2 = RECORD1 + 6,
        BOOK_SIZE = RECORD2 + 3
    };
    unsigned char book[BOOK_SIZE];
    QwMobiDocument *document = NULL;
    const unsigned char *text;
    size_t text_size = 0u;
    QwMobiStatus status;

    memset(book, 0, sizeof(book));
    memcpy(book + 60, "BOOK", 4u);
    memcpy(book + 64, "MOBI", 4u);
    put_be16(book + 76, 3u);
    put_be32(book + HEADER_SIZE, RECORD0);
    put_be32(book + HEADER_SIZE + 8, RECORD1);
    put_be32(book + HEADER_SIZE + 16, RECORD2);

    /* PalmDOC header: PalmDOC compression, 8 text bytes over 2 records of 4. */
    put_be16(book + RECORD0, 2u);
    put_be32(book + RECORD0 + 4, 8u);
    put_be16(book + RECORD0 + 8, 2u);
    put_be16(book + RECORD0 + 10, 4u);
    /* MOBI header: "MOBI", 232-byte header, UTF-8, version 6. */
    memcpy(book + RECORD0 + 16, "MOBI", 4u);
    put_be32(book + RECORD0 + 20, 232u);
    put_be32(book + RECORD0 + 28, 65001u);
    put_be32(book + RECORD0 + 36, 6u);

    /* Record 1: four literal 'A' bytes, then a padding byte. */
    book[RECORD1 + 0] = 4u;
    book[RECORD1 + 1] = 'A';
    book[RECORD1 + 2] = 'A';
    book[RECORD1 + 3] = 'A';
    book[RECORD1 + 4] = 'A';
    book[RECORD1 + 5] = 0u;
    /* Record 2: back-reference distance 4, length 4 -> "AAAA", then padding. */
    book[RECORD2 + 0] = 0x80u; /* 10xxxxxx */
    book[RECORD2 + 1] = 0x21u; /* distance 4, length 4 */
    book[RECORD2 + 2] = 0u;

    CHECK(qw_mobi_probe(book, sizeof(book)) == 1);
    status = qw_mobi_parse(book, sizeof(book), &document);
    if (status != QW_MOBI_OK) {
        fprintf(stderr, "parse status %d\n", (int)status);
    }
    CHECK(status == QW_MOBI_OK);
    CHECK(document != NULL);
    CHECK(qw_mobi_document_kind(document) == QW_MOBI_KIND_MOBI);
    text = qw_mobi_document_text(document, &text_size);
    CHECK(text != NULL);
    CHECK(text_size == 8u);
    CHECK(memcmp(text, "AAAAAAAA", 8u) == 0);
    qw_mobi_document_free(document);
    return 0;
}
