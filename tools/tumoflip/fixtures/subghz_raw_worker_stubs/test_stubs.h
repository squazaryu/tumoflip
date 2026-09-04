#pragma once

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FuriThread FuriThread;
typedef struct FuriStreamBuffer FuriStreamBuffer;
typedef struct FuriEventFlag FuriEventFlag;
typedef struct FuriString FuriString;
typedef struct Storage Storage;
typedef struct Stream Stream;
typedef struct FlipperFormat FlipperFormat;
typedef struct SubGhzDevice SubGhzDevice;

typedef int32_t (*FuriThreadCallback)(void* context);

enum {
    FuriFlagWaitAny = 0U,
    FuriFlagErrorTimeout = 0xFFFFFFFEU,
};

typedef struct {
    int level;
    uint32_t duration;
    bool reset;
    bool wait;
} LevelDuration;

#define LEVEL_DURATION_RESET 0
#define RECORD_STORAGE       "storage"
#define StreamOffsetFromCurrent 1
#define StrintParseNoError 0

#define furi_assert(condition) assert(condition)
#define furi_check(condition)  assert(condition)
#define UNUSED(value)          ((void)(value))
#define FURI_LOG_I(tag, format, ...) ((void)0)
#define FURI_LOG_E(tag, format, ...) ((void)0)

FuriThread* furi_thread_alloc_ex(
    const char* name,
    size_t stack_size,
    FuriThreadCallback callback,
    void* context);
void furi_thread_start(FuriThread* thread);
void furi_thread_join(FuriThread* thread);
void furi_thread_free(FuriThread* thread);
void furi_delay_ms(uint32_t duration_ms);

FuriEventFlag* furi_event_flag_alloc(void);
void furi_event_flag_free(FuriEventFlag* event);
uint32_t furi_event_flag_set(FuriEventFlag* event, uint32_t flags);
uint32_t furi_event_flag_clear(FuriEventFlag* event, uint32_t flags);
uint32_t furi_event_flag_wait(
    FuriEventFlag* event,
    uint32_t flags,
    uint32_t options,
    uint32_t timeout_ms);

FuriStreamBuffer* furi_stream_buffer_alloc(size_t size, size_t trigger_level);
void furi_stream_buffer_free(FuriStreamBuffer* stream);
void furi_stream_buffer_reset(FuriStreamBuffer* stream);
size_t furi_stream_buffer_send(
    FuriStreamBuffer* stream,
    const void* data,
    size_t size,
    uint32_t timeout_ms);
size_t furi_stream_buffer_receive(
    FuriStreamBuffer* stream,
    void* data,
    size_t size,
    uint32_t timeout_ms);
size_t furi_stream_buffer_bytes_available(FuriStreamBuffer* stream);
size_t furi_stream_buffer_spaces_available(FuriStreamBuffer* stream);

FuriString* furi_string_alloc(void);
void furi_string_free(FuriString* string);
void furi_string_set(FuriString* string, const char* value);
const char* furi_string_get_cstr(const FuriString* string);
void furi_string_printf(FuriString* string, const char* format, ...);
void furi_string_trim(FuriString* string);

Storage* furi_record_open(const char* record);
void furi_record_close(const char* record);

FlipperFormat* flipper_format_file_alloc(Storage* storage);
void flipper_format_free(FlipperFormat* format);
bool flipper_format_file_open_existing(FlipperFormat* format, const char* path);
bool flipper_format_read_string(
    FlipperFormat* format,
    const char* key,
    FuriString* value);
void flipper_format_file_close(FlipperFormat* format);
Stream* flipper_format_get_raw_stream(FlipperFormat* format);

size_t stream_size(Stream* stream);
size_t stream_tell(Stream* stream);
bool stream_seek(Stream* stream, int32_t offset, int origin);
bool stream_read_line(Stream* stream, FuriString* value);

int strint_to_int32(const char* input, char** end, int32_t* value, int base);

const SubGhzDevice* subghz_devices_get_by_name(const char* name);
bool subghz_devices_is_async_complete_tx(const SubGhzDevice* device);

static inline LevelDuration level_duration_make(bool level, uint32_t duration) {
    return (LevelDuration){.level = level, .duration = duration};
}

static inline LevelDuration level_duration_reset(void) {
    return (LevelDuration){.reset = true};
}

static inline LevelDuration level_duration_wait(void) {
    return (LevelDuration){.wait = true};
}
