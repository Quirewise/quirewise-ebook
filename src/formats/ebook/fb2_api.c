#include "quirewise/fb2.h"

#include "fb2_format.h"

#include <stdlib.h>

struct QwFB2Document {
    QwLayoutDocument layout;
};

static QwFB2Status fb2_status_from_parser(int status)
{
    switch (status) {
    case QW_EBOOK_OK: return QW_FB2_OK;
    case QW_EBOOK_ERR_INVALID_ARGUMENT: return QW_FB2_ERROR_INVALID_ARGUMENT;
    case QW_EBOOK_ERR_OUT_OF_MEMORY: return QW_FB2_ERROR_OUT_OF_MEMORY;
    case QW_EBOOK_ERR_FORMAT: return QW_FB2_ERROR_FORMAT;
    case QW_EBOOK_ERR_UNSUPPORTED: return QW_FB2_ERROR_UNSUPPORTED;
    case QW_EBOOK_ERR_DECODE: return QW_FB2_ERROR_DECODE;
    case QW_EBOOK_ERR_RESOURCE_LIMIT: return QW_FB2_ERROR_RESOURCE_LIMIT;
    default: return QW_FB2_ERROR_UNKNOWN;
    }
}

QwFB2Status qw_fb2_parse(const void *data, size_t size,
                         QwFB2Document **out_document)
{
    QwFB2Document *document;
    int status;
    if (!out_document) return QW_FB2_ERROR_INVALID_ARGUMENT;
    *out_document = NULL;
    document = (QwFB2Document *)calloc(1u, sizeof(*document));
    if (!document) return QW_FB2_ERROR_OUT_OF_MEMORY;
    if (!qw_fb2_probe_native(data, size)) {
        free(document);
        return QW_FB2_ERROR_FORMAT;
    }
    status = qw_fb2_native_open_layout(data, size, QW_FORMAT_FB2, NULL,
                                       &document->layout, NULL);
    if (status != QW_EBOOK_OK) {
        if (document->layout.dispose)
            document->layout.dispose(document->layout.context);
        free(document);
        return fb2_status_from_parser(status);
    }
    *out_document = document;
    return QW_FB2_OK;
}

const uint8_t *qw_fb2_document_html(const QwFB2Document *document,
                                   size_t *out_size)
{
    if (out_size) *out_size = 0u;
    if (!document || !out_size) return NULL;
    *out_size = document->layout.html_size;
    return document->layout.html_utf8;
}

void qw_fb2_document_free(QwFB2Document *document)
{
    if (!document) return;
    if (document->layout.dispose)
        document->layout.dispose(document->layout.context);
    free(document);
}
