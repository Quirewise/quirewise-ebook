#include "quirewise/pml.h"

#include "../../core/architecture_metadata.h"

#include <stdlib.h>
#include <string.h>

typedef struct QwPmlBuffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} QwPmlBuffer;

uint32_t qw_pml_abi_version(void)
{
    return QUIREWISE_PML_ABI_VERSION;
}

static int pml_append(QwPmlBuffer *buffer, const void *data, size_t size)
{
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (size > SIZE_MAX - buffer->size) return 0;
    required = buffer->size + size;
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256u;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2u) {
                capacity = required;
                break;
            }
            capacity *= 2u;
        }
        grown = (uint8_t *)realloc(buffer->data, capacity);
        if (!grown) return 0;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (size) memcpy(buffer->data + buffer->size, data, size);
    buffer->size = required;
    return 1;
}

static int pml_append_literal(QwPmlBuffer *buffer, const char *text)
{
    return pml_append(buffer, text, strlen(text));
}

QwPmlStatus qw_pml_parse(const void *pml_bytes, size_t pml_size,
                         QwPmlDocument *out_document)
{
    const uint8_t *data = (const uint8_t *)pml_bytes;
    QwPmlBuffer text = {0};
    size_t i = 0u;
    if (out_document) memset(out_document, 0, sizeof(*out_document));
    if (!data || !pml_size || !out_document)
        return QW_PML_ERR_INVALID_ARGUMENT;
    if (pml_size > QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRY_BYTES)
        return QW_PML_ERR_RESOURCE_LIMIT;

    while (i < pml_size) {
        if (data[i] == '\\' && i + 1u < pml_size) {
            uint8_t command = data[i + 1u];
            if (command == 'p') {
                if (!pml_append_literal(&text, "\n\n")) goto oom;
                i += 2u;
                continue;
            }
            if (command == 'n') {
                if (!pml_append_literal(&text, "\n")) goto oom;
                i += 2u;
                continue;
            }
            if (command == 't') {
                if (!pml_append_literal(&text, "\t")) goto oom;
                i += 2u;
                continue;
            }
            if (command == '\\') {
                if (!pml_append_literal(&text, "\\")) goto oom;
                i += 2u;
                continue;
            }
            if ((command >= 'A' && command <= 'Z') ||
                (command >= 'a' && command <= 'z')) {
                i += 2u;
                if ((command == 's' || command == 'S' ||
                     command == 'c' || command == 'C' ||
                     command == 'x' || command == 'X') &&
                    i < pml_size && data[i] >= '0' && data[i] <= '9') {
                    while (i < pml_size && data[i] >= '0' && data[i] <= '9') ++i;
                }
                continue;
            }
            if (!pml_append(&text, data + i + 1u, 1u)) goto oom;
            i += 2u;
            continue;
        }
        if (!pml_append(&text, data + i, 1u)) goto oom;
        ++i;
    }

    if (!text.data) {
        text.data = (uint8_t *)malloc(1u);
        if (!text.data) return QW_PML_ERR_OUT_OF_MEMORY;
    }
    out_document->text_bytes = text.data;
    out_document->text_size = text.size;
    return QW_PML_OK;

oom:
    free(text.data);
    return QW_PML_ERR_OUT_OF_MEMORY;
}

void qw_pml_document_free(QwPmlDocument *document)
{
    if (!document) return;
    free(document->text_bytes);
    memset(document, 0, sizeof(*document));
}

const char *qw_pml_status_string(int status)
{
    switch (status) {
    case QW_PML_OK: return "ok";
    case QW_PML_ERR_INVALID_ARGUMENT: return "invalid argument";
    case QW_PML_ERR_OUT_OF_MEMORY: return "out of memory";
    case QW_PML_ERR_RESOURCE_LIMIT: return "PML input resource limit exceeded";
    default: return "unknown PML status";
    }
}
