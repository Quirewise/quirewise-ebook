#include "quirewise/epub.h"

#include "epub_native.h"
#include "../../core/layout_document.h"

#include <stdlib.h>
#include <string.h>

struct QwEpubDocument {
    QwLayoutDocument layout;
};

static void epub_layout_dispose(QwLayoutDocument *layout)
{
    if (!layout) return;
    if (layout->dispose) layout->dispose(layout->context);
    memset(layout, 0, sizeof(*layout));
}

static QwEpubStatus epub_status_map(int status)
{
    switch (status) {
    case QW_EBOOK_OK: return QW_EPUB_OK;
    case QW_EBOOK_ERR_INVALID_ARGUMENT: return QW_EPUB_ERR_INVALID_ARGUMENT;
    case QW_EBOOK_ERR_OUT_OF_MEMORY: return QW_EPUB_ERR_OUT_OF_MEMORY;
    case QW_EBOOK_ERR_UNSUPPORTED: return QW_EPUB_ERR_UNSUPPORTED;
    case QW_EBOOK_ERR_RESOURCE_LIMIT: return QW_EPUB_ERR_RESOURCE_LIMIT;
    case QW_EBOOK_ERR_DECODE: return QW_EPUB_ERR_DECODE;
    default: return QW_EPUB_ERR_FORMAT;
    }
}

QwEpubStatus qw_epub_parse(const void *data, size_t size,
                           QwEpubDocument **out_document)
{
    QwEpubDocument *document;
    QwConvertReport report;
    int status;
    if (!out_document) return QW_EPUB_ERR_INVALID_ARGUMENT;
    *out_document = NULL;
    if (!data || !size) return QW_EPUB_ERR_INVALID_ARGUMENT;
    document = (QwEpubDocument *)calloc(1u, sizeof(*document));
    if (!document) return QW_EPUB_ERR_OUT_OF_MEMORY;
    memset(&report, 0, sizeof(report));
    status = qw_epub_native_open_layout(data, size, QW_FORMAT_EPUB, NULL,
                                        &document->layout, &report);
    if (status != QW_EBOOK_OK) {
        epub_layout_dispose(&document->layout);
        free(document);
        return epub_status_map(status);
    }
    *out_document = document;
    return QW_EPUB_OK;
}

void qw_epub_document_free(QwEpubDocument *document)
{
    if (!document) return;
    epub_layout_dispose(&document->layout);
    free(document);
}

size_t qw_epub_section_count(const QwEpubDocument *document)
{
    return document ? document->layout.section_count : 0u;
}

QwEpubStatus qw_epub_section_at(const QwEpubDocument *document,
                                size_t section_index,
                                QwEpubSectionView *out_section)
{
    const QwLayoutSection *section;
    if (!document || !out_section ||
        section_index >= document->layout.section_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    section = &document->layout.sections[section_index];
    out_section->title = section->title;
    out_section->title_size = section->title ? strlen(section->title) : 0u;
    out_section->paragraph_count = section->paragraph_count;
    return QW_EPUB_OK;
}

QwEpubStatus qw_epub_paragraph_at(const QwEpubDocument *document,
                                  size_t section_index,
                                  size_t paragraph_index,
                                  QwEpubParagraphView *out_paragraph)
{
    const QwLayoutSection *section;
    const QwLayoutParagraph *paragraph;
    if (!document || !out_paragraph ||
        section_index >= document->layout.section_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    section = &document->layout.sections[section_index];
    if (paragraph_index >= section->paragraph_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    paragraph = &section->paragraphs[paragraph_index];
    out_paragraph->heading_level = paragraph->heading_level;
    out_paragraph->page_break_before = paragraph->page_break_before;
    out_paragraph->run_count = paragraph->run_count;
    return QW_EPUB_OK;
}

QwEpubStatus qw_epub_run_at(const QwEpubDocument *document,
                            size_t section_index,
                            size_t paragraph_index,
                            size_t run_index,
                            QwEpubRunView *out_run)
{
    const QwLayoutSection *section;
    const QwLayoutParagraph *paragraph;
    const QwLayoutRun *run;
    if (!document || !out_run ||
        section_index >= document->layout.section_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    section = &document->layout.sections[section_index];
    if (paragraph_index >= section->paragraph_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    paragraph = &section->paragraphs[paragraph_index];
    if (run_index >= paragraph->run_count)
        return QW_EPUB_ERR_INVALID_ARGUMENT;
    run = &paragraph->runs[run_index];
    out_run->utf8_text = run->utf8_text;
    out_run->text_size = run->text_size;
    out_run->style_flags = run->style_flags;
    out_run->link_target = run->link_target;
    out_run->link_target_size = run->link_target
        ? strlen(run->link_target) : 0u;
    return QW_EPUB_OK;
}
