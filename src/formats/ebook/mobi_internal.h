#ifndef QUIREWISE_MOBI_INTERNAL_H
#define QUIREWISE_MOBI_INTERNAL_H

#include "../../core/format_registry.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwMobiBook {
    uint8_t *text;
    size_t text_size;
} QwMobiBook;

QwInputFormat qw_mobi_detect_type(const void *data, size_t size);

int qw_mobi_to_epub(const void *data, size_t size,
                    uint8_t **out_epub, size_t *out_size,
                    QwConvertReport *report);

int qw_mobi_parse_palm_doc(const void *data, size_t size,
                           QwMobiBook *out_book);
void qw_mobi_book_drop(QwMobiBook *book);

int qw_palm_doc_decode_record(const uint8_t *data, size_t size,
                              uint8_t *output, size_t output_start,
                              size_t output_capacity, size_t *out_size);

int qw_mobi_markup_to_text(const uint8_t *markup, size_t markup_size,
                           uint8_t **out_text, size_t *out_text_size);

int qw_mobi_text_write_epub(const char *title, const uint8_t *text,
                            size_t text_size, uint8_t **out_epub,
                            size_t *out_size);

#endif
