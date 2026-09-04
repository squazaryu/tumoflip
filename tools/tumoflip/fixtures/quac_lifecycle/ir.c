#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_I(...) ((void)0)
#define MAX_TIMINGS_AMOUNT 1024
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr, "IR invariant failed: %s\n", #x); return 1; } } while(0)
typedef struct { void* ptr; bool live; } Allocation;
static Allocation allocations[128];
static size_t allocation_count, live_count, invalid_frees;
static void* test_malloc(size_t size) {
    void* ptr = calloc(1, size ? size : 1); // Firmware malloc zeroes memory.
    assert(ptr && allocation_count < 128);
    allocations[allocation_count++] = (Allocation){ptr, true};
    live_count++;
    return ptr;
}
static void test_free(void* ptr) {
    if(!ptr) return;
    for(size_t i = allocation_count; i > 0; i--) {
        if(allocations[i - 1].ptr == ptr && allocations[i - 1].live) {
            allocations[i - 1].live = false;
            live_count--;
            free(ptr);
            return;
        }
    }
    invalid_frees++;
}
#define malloc test_malloc
#define free test_free
typedef struct { char text[80]; } FuriString;
typedef struct { bool raw; const char* fail; uint32_t count; size_t names; size_t writes; } FlipperFormat;
typedef enum { InfraredProtocolUnknown = -1, InfraredProtocolNEC } InfraredProtocol;
typedef struct { InfraredProtocol protocol; uint32_t address; uint32_t command; bool repeat; } InfraredMessage;
@SIGNAL_HEADER@
static FuriString* furi_string_alloc(void) { return malloc(sizeof(FuriString)); }
static void furi_string_free(FuriString* s) { free(s); }
static void set(FuriString* s, const char* v) { snprintf(s->text, sizeof(s->text), "%s", v); }
static const char* furi_string_get_cstr(FuriString* s) { return s->text; }
static int furi_string_cmp_str(FuriString* s, const char* v) { return strcmp(s->text, v); }
static bool furi_string_equal(FuriString* s, const char* v) { return !strcmp(s->text, v); }
static void furi_string_reset(FuriString* s) { s->text[0] = 0; }
static bool fails(FlipperFormat* f, const char* key) { return f->fail && !strcmp(f->fail, key); }
static bool flipper_format_read_header(FlipperFormat* f, FuriString* s, uint32_t* v) {
    f->names = 0; set(s, INFRARED_FILE_TYPE); *v = 1; return !fails(f, "header");
}
static bool flipper_format_read_string(FlipperFormat* f, const char* key, FuriString* s) {
    if(fails(f, key)) return false;
    if(!strcmp(key, "name")) { if(f->names++) return false; set(s, "Power"); }
    else if(!strcmp(key, "type")) set(s, f->raw ? "raw" : "parsed");
    else set(s, "NEC");
    return true;
}
static InfraredProtocol infrared_get_protocol_by_name(const char* name) { return InfraredProtocolNEC; }
static bool infrared_is_protocol_valid(InfraredProtocol p) { return p == InfraredProtocolNEC; }
static const char* infrared_get_protocol_name(InfraredProtocol p) { return "NEC"; }
static bool flipper_format_read_uint32(FlipperFormat* f, const char* key, uint32_t* v, uint32_t n) {
    if(fails(f, key)) return false;
    for(uint32_t i = 0; i < n; i++) v[i] = !strcmp(key, "frequency") ? 38000 : 100 + i;
    return true;
}
static bool flipper_format_read_float(FlipperFormat* f, const char* key, float* v, uint32_t n) {
    *v = 0.33f; return !fails(f, key);
}
static bool flipper_format_get_value_count(FlipperFormat* f, const char* key, uint32_t* n) {
    *n = f->count; return !fails(f, "count");
}
static bool flipper_format_read_hex(FlipperFormat* f, const char* key, uint8_t* v, uint32_t n) {
    memset(v, 0x12, n); return !fails(f, key);
}
static bool flipper_format_write_header_cstr(FlipperFormat* f, const char* t, uint32_t v) { f->writes++; return true; }
static bool flipper_format_write_comment_cstr(FlipperFormat* f, const char* t) { return true; }
static bool flipper_format_write_string(FlipperFormat* f, const char* k, FuriString* v) { return true; }
static bool flipper_format_write_string_cstr(FlipperFormat* f, const char* k, const char* v) { return true; }
static bool flipper_format_write_uint32(FlipperFormat* f, const char* k, uint32_t* v, uint32_t n) { assert(v && n); return true; }
static bool flipper_format_write_float(FlipperFormat* f, const char* k, float* v, uint32_t n) { return true; }
static bool flipper_format_write_hex(FlipperFormat* f, const char* k, uint8_t* v, uint32_t n) { return true; }

@PRODUCTION@

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* mode = argv[1];
    InfraredSignal* signal = infrared_utils_signal_alloc();
    FuriString* name = furi_string_alloc();
    FlipperFormat file = {.raw = true, .count = 3};
    bool expected = true;
    if(!strcmp(mode, "data-failure")) { file.fail = "data"; expected = false; }
    else if(!strcmp(mode, "early-raw-failure")) { file.fail = "frequency"; expected = false; }
    else if(!strcmp(mode, "invalid-count")) { file.count = MAX_TIMINGS_AMOUNT + 1; expected = false; }
    else if(!strcmp(mode, "empty-raw")) { file.count = 0; expected = false; }
    else if(!strcmp(mode, "missing-command")) { file.fail = "name"; expected = false; }
    else if(!strcmp(mode, "parsed-failure")) { file.raw = false; file.fail = "command"; expected = false; }
    else if(!strcmp(mode, "parsed")) file.raw = false;
    bool first = infrared_utils_read_signal_at_index(&file, 0, signal, name);
    REQUIRE(first == expected);
    if(!strcmp(mode, "raw-raw") || !strcmp(mode, "raw-parsed") || !strcmp(mode, "raw-failure")) {
        file.raw = strcmp(mode, "raw-parsed") != 0;
        file.fail = !strcmp(mode, "raw-failure") ? "type" : NULL;
        REQUIRE(infrared_utils_read_signal_at_index(&file, 0, signal, name) == (file.fail == NULL));
    } else if(!strcmp(mode, "parsed-raw-failure")) {
        file.raw = false;
        REQUIRE(infrared_utils_read_signal_at_index(&file, 0, signal, name));
        file.raw = true; file.fail = "frequency";
        REQUIRE(!infrared_utils_read_signal_at_index(&file, 0, signal, name));
    } else if(expected) {
        REQUIRE(signal->is_raw == file.raw);
        if(file.raw) {
            REQUIRE(signal->payload.raw.timings_size == 3);
            REQUIRE(signal->payload.raw.timings[2] == 102);
        } else REQUIRE(signal->payload.message.address == 0x12121212);
        REQUIRE(infrared_utils_write_signal(&file, signal, name));
        REQUIRE(file.writes == 1);
    }
    infrared_utils_signal_free(signal);
    furi_string_free(name);
    REQUIRE(invalid_frees == 0);
    REQUIRE(live_count == 0);
    return 0;
}
