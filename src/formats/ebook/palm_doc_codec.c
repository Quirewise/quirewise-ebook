#include "mobi_internal.h"

int qw_palm_doc_decode_record(const uint8_t *data, size_t size,
                              uint8_t *output, size_t output_capacity,
                              size_t *out_size)
{
    size_t input_position = 0u;
    size_t output_position = 0u;
    if ((!data && size) || !output || !out_size) return 0;
    *out_size = 0u;
    while (input_position < size) {
        uint8_t value = data[input_position++];
        if (value == 0u || (value >= 0x09u && value <= 0x7fu)) {
            if (output_position >= output_capacity) return 0;
            output[output_position++] = value;
        } else if (value <= 0x08u) {
            size_t literal_count = value;
            if (literal_count > size - input_position ||
                literal_count > output_capacity - output_position) return 0;
            for (size_t i = 0u; i < literal_count; ++i)
                output[output_position++] = data[input_position++];
        } else if (value <= 0xbfu) {
            uint8_t second;
            size_t distance;
            size_t length;
            if (input_position >= size) return 0;
            second = data[input_position++];
            distance = ((size_t)(value & 0x3fu) << 5) | (second >> 3);
            length = (size_t)(second & 0x07u) + 3u;
            if (!distance || distance > output_position ||
                length > output_capacity - output_position) return 0;
            for (size_t i = 0u; i < length; ++i) {
                output[output_position] = output[output_position - distance];
                ++output_position;
            }
        } else {
            if (output_capacity - output_position < 2u) return 0;
            output[output_position++] = (uint8_t)' ';
            output[output_position++] = (uint8_t)(value ^ 0x80u);
        }
    }
    *out_size = output_position;
    return 1;
}
