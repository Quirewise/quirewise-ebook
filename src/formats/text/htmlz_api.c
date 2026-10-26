#include "quirewise/htmlz.h"

#include "quirewise/archive.h"
#include "../../core/architecture_metadata.h"
#include "../../core/byte_utils.h"
#include "html_probe.h"

#include <stdlib.h>
#include <string.h>

enum {
    QW_HTMLZ_MAX_MEMBER_NAME_BYTES = 4096u
};

static const char htmlz_resource_limit_error[] =
    "HTMLZ archive exceeds its declared resource limits";

static int htmlz_span_equal_ascii(const uint8_t *bytes, size_t size,
                                  const char *text)
{
    size_t i;
    size_t text_size = strlen(text);
    if (!bytes || size != text_size) return 0;
    for (i = 0u; i < size; ++i) {
        if (qw_ascii_lower(bytes[i]) !=
            qw_ascii_lower((unsigned char)text[i])) return 0;
    }
    return 1;
}

static int htmlz_member_name(const uint8_t *name, size_t size,
                             int *out_is_index)
{
    size_t base = 0u;
    size_t base_size;
    if (!name || !size || !out_is_index) return 0;
    for (size_t i = 0u; i < size; ++i) {
        if (name[i] == '/' || name[i] == '\\') base = i + 1u;
    }
    if (base >= size) return 0;
    base_size = size - base;
    *out_is_index = htmlz_span_equal_ascii(name + base, base_size, "index.html") ||
                    htmlz_span_equal_ascii(name + base, base_size, "index.htm");
    return *out_is_index ||
           (base_size >= 5u &&
            htmlz_span_equal_ascii(name + size - 5u, 5u, ".html")) ||
           (base_size >= 4u &&
            htmlz_span_equal_ascii(name + size - 4u, 4u, ".htm")) ||
           (base_size >= 6u &&
            htmlz_span_equal_ascii(name + size - 6u, 6u, ".xhtml"));
}

static QwHtmlzStatus htmlz_map_archive_status(QwArchiveStatus status)
{
    if (status == QW_ARCHIVE_ERR_OUT_OF_MEMORY)
        return QW_HTMLZ_ERR_OUT_OF_MEMORY;
    if (status == QW_ARCHIVE_ERR_RESOURCE_LIMIT)
        return QW_HTMLZ_ERR_RESOURCE_LIMIT;
    return QW_HTMLZ_ERR_ARCHIVE;
}

uint32_t qw_htmlz_abi_version(void)
{
    return QUIREWISE_HTMLZ_ABI_VERSION;
}

QwHtmlzStatus qw_htmlz_extract_primary_document(
    const void *archive_bytes, size_t archive_size, QwHtmlzDocument *out_document,
    const char **out_error)
{
    QwZipArchiveReader *reader = NULL;
    QwArchiveStatus archive_status;
    size_t entry_count;
    const char *archive_error = NULL;
    uint64_t total_uncompressed_bytes = 0u;

    if (out_document) memset(out_document, 0, sizeof(*out_document));
    if (out_error) *out_error = NULL;
    if (!archive_bytes || !archive_size || !out_document)
        return QW_HTMLZ_ERR_INVALID_ARGUMENT;
    if (archive_size > QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_INPUT_BYTES) {
        if (out_error) *out_error = htmlz_resource_limit_error;
        return QW_HTMLZ_ERR_RESOURCE_LIMIT;
    }

    archive_status = qw_zip_archive_open(archive_bytes, archive_size,
                                         &reader, &archive_error);
    if (archive_status != QW_ARCHIVE_OK) {
        if (out_error) *out_error = archive_error;
        return htmlz_map_archive_status(archive_status);
    }

    entry_count = qw_zip_archive_entry_count(reader);
    if (entry_count > QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRIES) {
        if (out_error) *out_error = htmlz_resource_limit_error;
        qw_zip_archive_close(reader);
        return QW_HTMLZ_ERR_RESOURCE_LIMIT;
    }
    for (size_t i = 0u; i < entry_count; ++i) {
        QwArchiveEntry entry;
        if (qw_zip_archive_get_entry(reader, i, &entry) != QW_ARCHIVE_OK) {
            if (out_error) *out_error = qw_zip_archive_last_error(reader);
            qw_zip_archive_close(reader);
            return QW_HTMLZ_ERR_ARCHIVE;
        }
        if (entry.uncompressed_size >
                QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRY_BYTES ||
            total_uncompressed_bytes >
                QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_TOTAL_BYTES -
                    entry.uncompressed_size) {
            if (out_error) *out_error = htmlz_resource_limit_error;
            qw_zip_archive_close(reader);
            return QW_HTMLZ_ERR_RESOURCE_LIMIT;
        }
        total_uncompressed_bytes += entry.uncompressed_size;
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0u; i < entry_count; ++i) {
            QwArchiveEntry entry;
            QwArchiveBuffer extracted = {0};
            uint8_t *name_copy;
            int is_index = 0;

            if (qw_zip_archive_get_entry(reader, i, &entry) != QW_ARCHIVE_OK ||
                entry.is_directory || !entry.name_bytes || !entry.name_size ||
                entry.name_size > QW_HTMLZ_MAX_MEMBER_NAME_BYTES ||
                memchr(entry.name_bytes, 0, entry.name_size) ||
                !htmlz_member_name(entry.name_bytes, entry.name_size, &is_index) ||
                (pass == 0 && !is_index) || (pass == 1 && is_index) ||
                entry.uncompressed_size >
                    QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRY_BYTES ||
                entry.uncompressed_size > SIZE_MAX) {
                continue;
            }

            archive_status = qw_zip_archive_extract(reader, i, NULL, &extracted);
            if (archive_status != QW_ARCHIVE_OK) continue;
            if (extracted.size != (size_t)entry.uncompressed_size ||
                !qw_html_format_probe(extracted.data, extracted.size)) {
                qw_archive_buffer_free(&extracted);
                continue;
            }

            name_copy = (uint8_t *)malloc(entry.name_size + 1u);
            if (!name_copy) {
                qw_archive_buffer_free(&extracted);
                qw_zip_archive_close(reader);
                return QW_HTMLZ_ERR_OUT_OF_MEMORY;
            }
            memcpy(name_copy, entry.name_bytes, entry.name_size);
            name_copy[entry.name_size] = 0u;
            out_document->html_bytes = extracted.data;
            out_document->html_size = extracted.size;
            out_document->member_name_bytes = name_copy;
            out_document->member_name_size = entry.name_size;
            out_document->archive_entry_index = i;
            qw_zip_archive_close(reader);
            return QW_HTMLZ_OK;
        }
    }

    if (out_error) *out_error = qw_zip_archive_last_error(reader);
    qw_zip_archive_close(reader);
    return QW_HTMLZ_ERR_NOT_FOUND;
}

void qw_htmlz_document_free(QwHtmlzDocument *document)
{
    if (!document) return;
    free(document->html_bytes);
    free(document->member_name_bytes);
    memset(document, 0, sizeof(*document));
}

const char *qw_htmlz_status_string(int status)
{
    switch (status) {
    case QW_HTMLZ_OK: return "ok";
    case QW_HTMLZ_ERR_INVALID_ARGUMENT: return "invalid argument";
    case QW_HTMLZ_ERR_OUT_OF_MEMORY: return "out of memory";
    case QW_HTMLZ_ERR_ARCHIVE: return "invalid or unreadable archive";
    case QW_HTMLZ_ERR_NOT_FOUND: return "no supported HTML document was found";
    case QW_HTMLZ_ERR_RESOURCE_LIMIT: return "HTMLZ resource limit exceeded";
    default: return "unknown HTMLZ status";
    }
}
