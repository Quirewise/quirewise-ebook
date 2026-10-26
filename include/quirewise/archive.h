#ifndef QUIREWISE_ARCHIVE_H
#define QUIREWISE_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(QUIREWISE_ARCHIVE_BUILD_SHARED)
#  if defined(QUIREWISE_ARCHIVE_EXPORTS)
#    define QUIREWISE_ARCHIVE_API __declspec(dllexport)
#  else
#    define QUIREWISE_ARCHIVE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(QUIREWISE_ARCHIVE_BUILD_SHARED)
#  define QUIREWISE_ARCHIVE_API __attribute__((visibility("default")))
#else
#  define QUIREWISE_ARCHIVE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QUIREWISE_ARCHIVE_ABI_VERSION 1u

typedef enum QwArchiveStatus {
    QW_ARCHIVE_OK = 0,
    QW_ARCHIVE_ERR_INVALID_ARGUMENT = -1,
    QW_ARCHIVE_ERR_OUT_OF_MEMORY = -2,
    QW_ARCHIVE_ERR_OPEN = -3,
    QW_ARCHIVE_ERR_UNSUPPORTED = -4,
    QW_ARCHIVE_ERR_EXTRACT = -5,
    QW_ARCHIVE_ERR_INDEX = -6,
    QW_ARCHIVE_ERR_CALLBACK = -7,
    QW_ARCHIVE_ERR_FORMAT = -8,
    QW_ARCHIVE_ERR_RESOURCE_LIMIT = -9
} QwArchiveStatus;

typedef struct QwZipArchiveReader QwZipArchiveReader;

/* Filename bytes are borrowed from the reader and valid until it is closed.
 * Their encoding follows the ZIP entry flags and may not be UTF-8. */
typedef struct QwArchiveEntry {
    const uint8_t *name_bytes;
    size_t name_size;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint16_t flags;
    uint16_t compression_method;
    int is_directory;
} QwArchiveEntry;

/* Extracted data is owned by the caller and released with
 * qw_archive_buffer_free. */
typedef struct QwArchiveBuffer {
    uint8_t *data;
    size_t size;
} QwArchiveBuffer;

typedef enum QwArchiveTarVisitAction {
    QW_ARCHIVE_TAR_CONTINUE = 0,
    QW_ARCHIVE_TAR_STOP = 1
} QwArchiveTarVisitAction;

/* Name and data are borrowed from the input archive and valid only during the
 * callback. The visitor is called for regular files, not directory entries. */
typedef QwArchiveTarVisitAction (*QwArchiveTarVisitor)(
    void *context, const char *name, const uint8_t *data, size_t size);

QUIREWISE_ARCHIVE_API uint32_t qw_archive_abi_version(void);

/* Opens an in-memory ZIP archive. The input bytes are borrowed and must remain
 * alive until qw_zip_archive_close. On failure, out_error may receive a static
 * diagnostic string owned by the library. */
QUIREWISE_ARCHIVE_API QwArchiveStatus qw_zip_archive_open(
    const void *data, size_t size, QwZipArchiveReader **out_reader,
    const char **out_error);
QUIREWISE_ARCHIVE_API void qw_zip_archive_close(QwZipArchiveReader *reader);
QUIREWISE_ARCHIVE_API size_t qw_zip_archive_entry_count(
    const QwZipArchiveReader *reader);
QUIREWISE_ARCHIVE_API QwArchiveStatus qw_zip_archive_get_entry(
    const QwZipArchiveReader *reader, size_t index, QwArchiveEntry *out_entry);
/* Pass NULL for unencrypted entries or a traditional ZipCrypto password. */
QUIREWISE_ARCHIVE_API QwArchiveStatus qw_zip_archive_extract(
    QwZipArchiveReader *reader, size_t index, const char *password,
    QwArchiveBuffer *out_buffer);
/* Visits regular files in a USTAR archive. Return STOP to end successfully;
 * any other nonzero visitor result aborts with QW_ARCHIVE_ERR_CALLBACK. */
QUIREWISE_ARCHIVE_API QwArchiveStatus qw_tar_archive_visit_ustar(
    const void *data, size_t size, QwArchiveTarVisitor visitor,
    void *visitor_context);
QUIREWISE_ARCHIVE_API void qw_archive_buffer_free(QwArchiveBuffer *buffer);
/* Returns the last native ZIP diagnostic, or NULL when there is no error. */
QUIREWISE_ARCHIVE_API const char *qw_zip_archive_last_error(
    const QwZipArchiveReader *reader);
QUIREWISE_ARCHIVE_API const char *qw_archive_status_string(int status);

#ifdef __cplusplus
}
#endif

#endif
