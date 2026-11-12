#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CORPUS_RAW_MAX_BYTES  (4U * 1024U * 1024U)
#define CORPUS_RAW_MAX_PULSES 500000U
typedef bool (*CorpusRawEmit)(bool level, uint32_t duration, void* context);
typedef struct {
    CorpusRawEmit emit;
    void* context;
    uint32_t bytes;
    uint32_t pulses;
    uint32_t frequency;
    uint32_t number;
    char key[32];
    char value[512];
    char preset[64];
    uint8_t key_length;
    uint16_t value_length;
    uint8_t headers;
    bool colon;
    bool raw;
    bool negative;
    bool digits;
    bool token;
    bool failed;
} CorpusRaw;

void corpus_raw_init(CorpusRaw* parser, CorpusRawEmit emit, void* context);
bool corpus_raw_feed(CorpusRaw* parser, const char* bytes, size_t count);
bool corpus_raw_finish(CorpusRaw* parser);
