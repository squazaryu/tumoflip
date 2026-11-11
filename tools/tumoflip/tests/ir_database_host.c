#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Error codes are supplied from the production header below. */
typedef struct { char text[64]; } FuriString;
typedef struct { bool valid; } InfraredSignal;
typedef struct { size_t positions[8]; size_t count; } Positions;
typedef struct { Positions signals; } InfraredBruteForceRecord;
typedef struct { const char* db_filename; bool is_started; int records; } InfraredBruteForce;
typedef struct { unsigned position; } FlipperFormat;
typedef int Storage;
static Storage storage;
static unsigned scratch, records_open, next_frame, frame_count;
static bool can_open = true, body_valid = true;
static uint32_t body_error;
static const char* frames[8];
static InfraredBruteForceRecord record;

#define furi_check assert
/* Match a release firmware: asserts must NOT protect the tested input. */
#define furi_assert(...) ((void)0)
#define RECORD_STORAGE "storage"
static void* alloc_scratch(size_t n) { scratch++; return calloc(1, n); }
static void free_scratch(void* p) { assert(p && scratch); scratch--; free(p); }
static Storage* furi_record_open(const char* name) { assert(!strcmp(name, RECORD_STORAGE)); records_open++; return &storage; }
static void furi_record_close(const char* name) { assert(!strcmp(name, RECORD_STORAGE) && records_open); records_open--; }
static FlipperFormat* flipper_format_buffered_file_alloc(Storage* s) { assert(s == &storage); return alloc_scratch(sizeof(FlipperFormat)); }
static bool flipper_format_buffered_file_open_existing(FlipperFormat* f, const char* path) { assert(f && path); return can_open; }
static void flipper_format_free(FlipperFormat* f) { free_scratch(f); }
static size_t flipper_format_tell(FlipperFormat* f) { return f->position; }
static FuriString* furi_string_alloc(void) { return alloc_scratch(sizeof(FuriString)); }
static void furi_string_free(FuriString* s) { free_scratch(s); }
static InfraredSignal* infrared_signal_alloc(void) { return alloc_scratch(sizeof(InfraredSignal)); }
static void infrared_signal_free(InfraredSignal* s) { free_scratch(s); }
static uint32_t infrared_signal_read_name(FlipperFormat* f, FuriString* name) {
    if(next_frame == frame_count) return 0x80000400;
    assert(strlen(frames[next_frame]) < sizeof(name->text));
    strcpy(name->text, frames[next_frame++]);
    f->position = next_frame * 10;
    return 0;
}
static uint32_t infrared_signal_read_body(InfraredSignal* s, FlipperFormat* f) { assert(f); s->valid = body_valid; return body_error; }
static bool infrared_signal_is_valid(InfraredSignal* s) { return s->valid; }
static InfraredBruteForceRecord* InfraredBruteForceRecordDict_get(int records, FuriString* name) {
    (void)records;
    return !strcmp(name->text, "Power") ? &record : NULL;
}
static void positions_push(Positions* p, size_t position) {
    assert(p && p->count < 8);
    p->positions[p->count++] = position;
}
#define SignalPositionArray_push_back(array, position) positions_push(&(array), position)

/* PRODUCTION */

static void reset(void) {
    assert(!scratch && !records_open);
    memset(&record, 0, sizeof(record));
    next_frame = frame_count = 0;
    can_open = body_valid = true;
    body_error = 0;
}
static uint32_t calculate(void) {
    InfraredBruteForce instance = {.db_filename = "fake-read-only.ir"};
    uint32_t error = infrared_brute_force_calculate_messages(&instance);
    assert(!scratch && !records_open && !instance.is_started);
    return error;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    reset();
    if(!strcmp(argv[1], "unknown")) {
        frames[0] = "UnknownButton"; frame_count = 1;
        assert(calculate() == InfraredErrorCodeSignalNameNotFound);
        assert(record.signals.count == 0);
    } else if(!strcmp(argv[1], "mixed")) {
        frames[0] = "UnknownButton"; frames[1] = "Power"; frames[2] = "Other"; frame_count = 3;
        assert(calculate() == InfraredErrorCodeNone);
        assert(record.signals.count == 1 && record.signals.positions[0] == 20);
    } else if(!strcmp(argv[1], "valid")) {
        frames[0] = frames[1] = "Power"; frame_count = 2;
        assert(calculate() == InfraredErrorCodeNone);
        assert(record.signals.count == 2 && record.signals.positions[0] == 10 && record.signals.positions[1] == 20);
    } else if(!strcmp(argv[1], "invalid")) {
        frames[0] = "Power"; frame_count = 1; body_error = InfraredErrorCodeSignalMessageUnableToReadProtocol;
        assert(calculate() == body_error && !record.signals.count);
        reset(); frames[0] = "Power"; frame_count = 1; body_valid = false;
        assert(calculate() == InfraredErrorCodeSignalMessageIsInvalid && !record.signals.count);
    } else if(!strcmp(argv[1], "empty")) {
        assert(calculate() == InfraredErrorCodeSignalNameNotFound);
        reset(); can_open = false;
        assert(calculate() == InfraredErrorCodeFileOperationFailed);
    } else if(!strcmp(argv[1], "unconfigured")) {
        InfraredBruteForce instance = {0};
        assert(infrared_brute_force_calculate_messages(&instance) == InfraredErrorCodeFileOperationFailed);
        assert(!scratch && !records_open);
    } else {
        assert(false);
    }
    return 0;
}
