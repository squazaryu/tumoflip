#include "corpus_raw.h"
#include <string.h>

void corpus_raw_init(CorpusRaw* p, CorpusRawEmit emit, void* context) {
    memset(p, 0, sizeof(*p));
    p->emit = emit;
    p->context = context;
}

static bool corpus_raw_token(CorpusRaw* p) {
    if(!p->token) return true;
    if(!p->digits || !p->number || p->number > 10000000 || p->pulses >= CORPUS_RAW_MAX_PULSES)
        return false;
    if(!p->emit(!p->negative, p->number, p->context)) return false;
    p->pulses++;
    p->number = 0;
    p->negative = p->digits = p->token = false;
    return true;
}

static bool corpus_raw_line(CorpusRaw* p) {
    if(p->raw) return corpus_raw_token(p);
    if(!p->key_length || p->key[0] == '#') return true;
    if(!p->colon || p->pulses) return false;
    const char* value = p->value;
    while(*value == ' ' || *value == '\t')
        value++;
    uint8_t bit = 0;
    if(!strcmp(p->key, "Filetype")) {
        bit = 1;
        if(strcmp(value, "Flipper SubGhz RAW File")) return false;
    } else if(!strcmp(p->key, "Version")) {
        bit = 2;
        if(strcmp(value, "1")) return false;
    } else if(!strcmp(p->key, "Frequency")) {
        bit = 4;
        uint32_t f = 0;
        for(const char* c = value; *c; c++) {
            if(*c < '0' || *c > '9' || f > (UINT32_MAX - (*c - '0')) / 10) return false;
            f = f * 10 + (*c - '0');
        }
        if(!f) return false;
        p->frequency = f;
    } else if(!strcmp(p->key, "Preset")) {
        bit = 8;
        if(!*value || strlen(value) >= sizeof(p->preset)) return false;
        strcpy(p->preset, value);
    } else if(!strcmp(p->key, "Protocol")) {
        bit = 16;
        if(strcmp(value, "RAW")) return false;
    }
    if(bit && (p->headers & bit)) return false;
    p->headers |= bit;
    return true;
}

bool corpus_raw_feed(CorpusRaw* p, const char* bytes, size_t count) {
    if(p->failed || (!bytes && count) || count > CORPUS_RAW_MAX_BYTES - p->bytes) {
        p->failed = true;
        return false;
    }
    p->bytes += count;
    for(size_t i = 0; i < count; i++) {
        char c = bytes[i];
        if(c == '\r') continue;
        if(!c) goto fail;
        if(c == '\n') {
            if(!corpus_raw_line(p)) goto fail;
            p->key_length = p->value_length = 0;
            p->key[0] = p->value[0] = 0;
            p->colon = p->raw = false;
        } else if(p->raw) {
            if(c == ' ' || c == '\t') {
                if(!corpus_raw_token(p)) goto fail;
            } else if(c == '-' && !p->token) {
                p->negative = p->token = true;
            } else if(c >= '0' && c <= '9') {
                if(p->number > (10000000U - (c - '0')) / 10) goto fail;
                p->number = p->number * 10 + (c - '0');
                p->digits = p->token = true;
            } else
                goto fail;
        } else if(!p->colon && c == ':') {
            p->colon = true;
            if(!strcmp(p->key, "RAW_Data")) {
                if(p->headers != 31) goto fail;
                p->raw = true;
            }
        } else if(!p->colon) {
            if(p->key_length >= sizeof(p->key) - 1) goto fail;
            p->key[p->key_length++] = c;
            p->key[p->key_length] = 0;
        } else {
            if(p->value_length >= sizeof(p->value) - 1) goto fail;
            p->value[p->value_length++] = c;
            p->value[p->value_length] = 0;
        }
    }
    return true;
fail:
    p->failed = true;
    return false;
}

bool corpus_raw_finish(CorpusRaw* p) {
    if(!p->failed && !corpus_raw_line(p)) p->failed = true;
    return !p->failed && p->headers == 31 && p->pulses;
}
