#include "quirewise/mobi.h"

#include "mobi_internal.h"

#include <stdlib.h>

struct QwMobiDocument {
    QwMobiBook book;
    QwMobiKind kind;
};

static QwMobiStatus mobi_status_from_parser(int status)
{
    switch (status) {
    case QW_EBOOK_OK: return QW_MOBI_OK;
    case QW_EBOOK_ERR_INVALID_ARGUMENT: return QW_MOBI_ERROR_INVALID_ARGUMENT;
    case QW_EBOOK_ERR_OUT_OF_MEMORY: return QW_MOBI_ERROR_OUT_OF_MEMORY;
    case QW_EBOOK_ERR_FORMAT: return QW_MOBI_ERROR_FORMAT;
    case QW_EBOOK_ERR_UNSUPPORTED: return QW_MOBI_ERROR_UNSUPPORTED;
    case QW_EBOOK_ERR_DECODE: return QW_MOBI_ERROR_DECODE;
    case QW_EBOOK_ERR_RESOURCE_LIMIT: return QW_MOBI_ERROR_RESOURCE_LIMIT;
    default: return QW_MOBI_ERROR_UNKNOWN;
    }
}

int qw_mobi_probe(const void *data, size_t size)
{
    return qw_mobi_detect_type(data, size) != QW_FORMAT_UNKNOWN;
}

QwMobiStatus qw_mobi_parse(const void *data, size_t size,
                           QwMobiDocument **out_document)
{
    QwMobiDocument *document;
    QwInputFormat format;
    int status;

    if (!out_document) return QW_MOBI_ERROR_INVALID_ARGUMENT;
    *out_document = NULL;
    format = qw_mobi_detect_type(data, size);
    if (format == QW_FORMAT_UNKNOWN) return QW_MOBI_ERROR_FORMAT;
    document = (QwMobiDocument *)calloc(1u, sizeof(*document));
    if (!document) return QW_MOBI_ERROR_OUT_OF_MEMORY;

    status = qw_mobi_parse_palm_doc(data, size, &document->book);
    if (status != QW_EBOOK_OK) {
        qw_mobi_book_drop(&document->book);
        free(document);
        return mobi_status_from_parser(status);
    }
    document->kind = format == QW_FORMAT_AZW3
        ? QW_MOBI_KIND_AZW3 : QW_MOBI_KIND_MOBI;
    *out_document = document;
    return QW_MOBI_OK;
}

QwMobiKind qw_mobi_document_kind(const QwMobiDocument *document)
{
    return document ? document->kind : QW_MOBI_KIND_INVALID;
}

const uint8_t *qw_mobi_document_text(const QwMobiDocument *document,
                                     size_t *out_size)
{
    if (out_size) *out_size = 0u;
    if (!document || !out_size) return NULL;
    *out_size = document->book.text_size;
    return document->book.text;
}

void qw_mobi_document_free(QwMobiDocument *document)
{
    if (!document) return;
    qw_mobi_book_drop(&document->book);
    free(document);
}
