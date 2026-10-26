#ifndef QUIREWISE_FB2_FORMAT_INTERNAL_H
#define QUIREWISE_FB2_FORMAT_INTERNAL_H

#include "../../core/format_registry.h"
#include "../../core/layout_document.h"

extern const QwFormatDescriptor qw_format_fb2;

int qw_fb2_native_open_layout(const void *data, size_t size,
                              QwInputFormat input_format,
                              const QwConvertOptions *options,
                              QwLayoutDocument *out_document,
                              QwConvertReport *report);
int qw_fb2_probe_native(const void *data, size_t size);

#endif
