#include "mobi_internal.h"

#include "../../core/byte_utils.h"

#include <string.h>

QwInputFormat qw_mobi_detect_type(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t record_count;
    size_t first_record;
    size_t mobi_header;
    uint32_t version;
    if (!bytes || size < 86u || memcmp(bytes + 60u, "BOOK", 4u) != 0 ||
        memcmp(bytes + 64u, "MOBI", 4u) != 0) {
        return QW_FORMAT_UNKNOWN;
    }
    record_count = qw_read_u16_be(bytes + 76u);
    if (!record_count || record_count > (SIZE_MAX - 78u) / 8u ||
        78u + record_count * 8u > size) {
        return QW_FORMAT_UNKNOWN;
    }
    first_record = qw_read_u32_be(bytes + 78u);
    if (first_record > size || size - first_record < 16u + 0x6cu) {
        return QW_FORMAT_UNKNOWN;
    }
    mobi_header = first_record + 16u;
    if (memcmp(bytes + mobi_header, "MOBI", 4u) != 0) {
        return QW_FORMAT_UNKNOWN;
    }
    version = qw_read_u32_be(bytes + mobi_header + 20u);
    return version >= 8u ? QW_FORMAT_AZW3 : QW_FORMAT_MOBI;
}
