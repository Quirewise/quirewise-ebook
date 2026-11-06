#include <quirewise/epub.h>
#include <quirewise/fb2.h>
#include <quirewise/htmlz.h>
#include <quirewise/mobi.h>
#include <quirewise/pml.h>
#include <quirewise/text_archive.h>

#include "ebook_seeds.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED UINT32_C(0x51f0dd99)
#define FUZZ_RUNS 150u

static uint32_t rng_state = FUZZ_SEED;

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    rng_state = value;
    return value;
}

static size_t mutate(const unsigned char *seed, size_t seed_size,
                     unsigned char *buffer, size_t capacity)
{
    size_t length = seed_size > capacity ? capacity : seed_size;
    unsigned int flips = 1u + next_random() % 4u;
    size_t index;
    memcpy(buffer, seed, length);
    if (next_random() % 8u == 0u && length > 1u)
        length = 1u + next_random() % length;
    for (index = 0u; index < flips && length; ++index) {
        size_t position = next_random() % length;
        buffer[position] = (unsigned char)(buffer[position] ^
                                           (unsigned char)(1u << (next_random() % 8u)));
    }
    return length;
}

int main(void)
{
    static const char fb2_seed[] =
        "<?xml version=\"1.0\"?><FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<body><section><title><p>Title</p></title><p>Hello fuzz.</p></section></body>"
        "</FictionBook>";
    static const char pml_seed[] =
        "\\B\\cTitle\\b\\c\n\\p Hello fuzz.\n";
    unsigned char buffer[4096];
    size_t run;
    size_t ok = 0u;
    size_t rejected = 0u;
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwEpubDocument *document = NULL;
        size_t size = mutate(epub_seed, sizeof(epub_seed), buffer,
                             sizeof(buffer));
        if (qw_epub_parse(buffer, size, &document) == QW_EPUB_OK && document) {
            ++ok;
            qw_epub_document_free(document);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwHtmlzDocument document = {0};
        const char *error = NULL;
        size_t size = mutate(htmlz_seed, sizeof(htmlz_seed), buffer,
                             sizeof(buffer));
        if (qw_htmlz_extract_primary_document(buffer, size, &document,
                                              &error) == QW_HTMLZ_OK) {
            ++ok;
            qw_htmlz_document_free(&document);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwTextArchiveDocument document = {0};
        const char *error = NULL;
        size_t size = mutate(txtz_seed, sizeof(txtz_seed), buffer,
                             sizeof(buffer));
        if (qw_text_archive_extract_primary(buffer, size, QW_TEXT_ARCHIVE_TXTZ,
                                            &document, &error) ==
            QW_TEXT_ARCHIVE_OK) {
            ++ok;
            qw_text_archive_document_free(&document);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwFB2Document *document = NULL;
        size_t size = mutate((const unsigned char *)fb2_seed,
                             sizeof(fb2_seed) - 1u, buffer, sizeof(buffer));
        if (qw_fb2_parse(buffer, size, &document) == QW_FB2_OK && document) {
            ++ok;
            qw_fb2_document_free(document);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwPmlDocument document = {0};
        size_t size = mutate((const unsigned char *)pml_seed,
                             sizeof(pml_seed) - 1u, buffer, sizeof(buffer));
        if (qw_pml_parse(buffer, size, &document) == QW_PML_OK) {
            ++ok;
            qw_pml_document_free(&document);
        } else {
            ++rejected;
        }
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        QwMobiDocument *document = NULL;
        size_t size = 32u + (size_t)(next_random() % 512u);
        size_t index;
        for (index = 0u; index < size; ++index)
            buffer[index] = (unsigned char)next_random();
        memcpy(buffer, "BOOKMOBI", 8u);
        if (qw_mobi_parse(buffer, size, &document) == QW_MOBI_OK && document) {
            ++ok;
            qw_mobi_document_free(document);
        } else {
            ++rejected;
        }
    }
    printf("Ebook fuzz smoke: %zu ok, %zu rejected, seed=0x%08x runs=%u\n", ok,
           rejected, FUZZ_SEED, FUZZ_RUNS);
    return 0;
}
