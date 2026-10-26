#ifndef QUIREWISE_HTMLZ_H
#define QUIREWISE_HTMLZ_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(QUIREWISE_HTMLZ_BUILD_SHARED)
#  if defined(QUIREWISE_HTMLZ_EXPORTS)
#    define QUIREWISE_HTMLZ_API __declspec(dllexport)
#  else
#    define QUIREWISE_HTMLZ_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(QUIREWISE_HTMLZ_BUILD_SHARED)
#  define QUIREWISE_HTMLZ_API __attribute__((visibility("default")))
#else
#  define QUIREWISE_HTMLZ_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QUIREWISE_HTMLZ_ABI_VERSION 1u

typedef enum QwHtmlzStatus {
    QW_HTMLZ_OK = 0,
    QW_HTMLZ_ERR_INVALID_ARGUMENT = -1,
    QW_HTMLZ_ERR_OUT_OF_MEMORY = -2,
    QW_HTMLZ_ERR_ARCHIVE = -3,
    QW_HTMLZ_ERR_NOT_FOUND = -4,
    QW_HTMLZ_ERR_RESOURCE_LIMIT = -5
} QwHtmlzStatus;

/* html_bytes and member_name_bytes are owned by the document. The name bytes
 * retain the ZIP entry-name encoding and are not guaranteed to be UTF-8. */
typedef struct QwHtmlzDocument {
    uint8_t *html_bytes;
    size_t html_size;
    uint8_t *member_name_bytes;
    size_t member_name_size;
    size_t archive_entry_index;
} QwHtmlzDocument;

QUIREWISE_HTMLZ_API uint32_t qw_htmlz_abi_version(void);

/* Selects index.html/index.htm first, then the first supported .html, .htm,
 * or .xhtml member containing an HTML root/doctype marker. Input bytes are
 * borrowed for the call; returned buffers are released by qw_htmlz_document_free. */
QUIREWISE_HTMLZ_API QwHtmlzStatus qw_htmlz_extract_primary_document(
    const void *archive_bytes, size_t archive_size, QwHtmlzDocument *out_document,
    const char **out_error);
QUIREWISE_HTMLZ_API void qw_htmlz_document_free(QwHtmlzDocument *document);
QUIREWISE_HTMLZ_API const char *qw_htmlz_status_string(int status);

#ifdef __cplusplus
}
#endif

#endif
