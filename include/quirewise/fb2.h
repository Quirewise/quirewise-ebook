#ifndef QUIREWISE_FB2_H
#define QUIREWISE_FB2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QwFB2Document QwFB2Document;

typedef enum QwFB2Status {
    QW_FB2_OK = 0,
    QW_FB2_ERROR_INVALID_ARGUMENT = -1,
    QW_FB2_ERROR_OUT_OF_MEMORY = -2,
    QW_FB2_ERROR_FORMAT = -4,
    QW_FB2_ERROR_UNSUPPORTED = -5,
    QW_FB2_ERROR_DECODE = -6,
    QW_FB2_ERROR_RESOURCE_LIMIT = -11,
    QW_FB2_ERROR_UNKNOWN = -100
} QwFB2Status;

QwFB2Status qw_fb2_parse(const void *data, size_t size,
                         QwFB2Document **out_document);
const uint8_t *qw_fb2_document_html(const QwFB2Document *document,
                                   size_t *out_size);
void qw_fb2_document_free(QwFB2Document *document);

#ifdef __cplusplus
}
#endif

#endif
