#ifndef QUIREWISE_EPUB_H
#define QUIREWISE_EPUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QwEpubStatus {
    QW_EPUB_OK = 0,
    QW_EPUB_ERR_INVALID_ARGUMENT = -1,
    QW_EPUB_ERR_OUT_OF_MEMORY = -2,
    QW_EPUB_ERR_FORMAT = -3,
    QW_EPUB_ERR_UNSUPPORTED = -4,
    QW_EPUB_ERR_RESOURCE_LIMIT = -5,
    QW_EPUB_ERR_DECODE = -6
} QwEpubStatus;

typedef enum QwEpubStyleFlags {
    QW_EPUB_STYLE_BOLD = 1u << 0,
    QW_EPUB_STYLE_ITALIC = 1u << 1,
    QW_EPUB_STYLE_CODE = 1u << 2,
    QW_EPUB_STYLE_HEADING = 1u << 3,
    QW_EPUB_STYLE_QUOTE = 1u << 4
} QwEpubStyleFlags;

typedef struct QwEpubDocument QwEpubDocument;

typedef struct QwEpubSectionView {
    const char *title;
    size_t title_size;
    size_t paragraph_count;
} QwEpubSectionView;

typedef struct QwEpubParagraphView {
    unsigned heading_level;
    unsigned page_break_before;
    size_t run_count;
} QwEpubParagraphView;

typedef struct QwEpubRunView {
    const uint8_t *utf8_text;
    size_t text_size;
    unsigned style_flags;
    const char *link_target;
    size_t link_target_size;
} QwEpubRunView;

/* The parser owns returned text; views remain valid until the document is freed. */
QwEpubStatus qw_epub_parse(const void *data, size_t size,
                           QwEpubDocument **out_document);
void qw_epub_document_free(QwEpubDocument *document);
size_t qw_epub_section_count(const QwEpubDocument *document);
QwEpubStatus qw_epub_section_at(const QwEpubDocument *document,
                                size_t section_index,
                                QwEpubSectionView *out_section);
QwEpubStatus qw_epub_paragraph_at(const QwEpubDocument *document,
                                  size_t section_index,
                                  size_t paragraph_index,
                                  QwEpubParagraphView *out_paragraph);
QwEpubStatus qw_epub_run_at(const QwEpubDocument *document,
                            size_t section_index,
                            size_t paragraph_index,
                            size_t run_index,
                            QwEpubRunView *out_run);

#ifdef __cplusplus
}
#endif

#endif
