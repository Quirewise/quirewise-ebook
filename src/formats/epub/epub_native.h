#ifndef QW_EPUB_NATIVE_H
#define QW_EPUB_NATIVE_H

#include "../../core/format_registry.h"
#include "../../core/layout_document.h"

extern const QwFormatDescriptor qw_format_epub;

int qw_epub_native_convert(const void *data, size_t size,
                           const QwConvertOptions *options,
                           uint8_t **out_pdf, size_t *out_size,
                           QwConvertReport *report);

int qw_epub_native_open_layout(const void *data, size_t size,
                               QwInputFormat input_format,
                               const QwConvertOptions *options,
                               QwLayoutDocument *out_document,
                               QwConvertReport *report);

#endif
