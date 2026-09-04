#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <time.h>

#include <flipper_format/flipper_format.h>
#include <lib/subghz/subghz_file_encoder_worker.h>

struct FuriThread {
    pthread_t thread;
    FuriThreadCallback callback;
    void* context;
    bool started;
};

struct FuriEventFlag {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint32_t flags;
};

struct FuriStreamBuffer {
    size_t capacity;
    size_t available;
};

struct FuriString {
    char* value;
};

struct Storage {
    int unused;
};

struct Stream {
    size_t offset;
    bool returned_line;
};

struct FlipperFormat {
    Stream stream;
};

struct SubGhzDevice {
    int unused;
};

static Storage storage;
static SubGhzDevice radio;
static unsigned open_count;
static unsigned fail_open_number;
static unsigned async_tx_checks;

static void* host_thread_entry(void* context) {
    FuriThread* thread = context;
    return (void*)(intptr_t)thread->callback(thread->context);
}

FuriThread* furi_thread_alloc_ex(
    const char* name,
    size_t stack_size,
    FuriThreadCallback callback,
    void* context) {
    UNUSED(name);
    UNUSED(stack_size);
    FuriThread* thread = calloc(1, sizeof(FuriThread));
    assert(thread);
    thread->callback = callback;
    thread->context = context;
    return thread;
}

void furi_thread_start(FuriThread* thread) {
    assert(!thread->started);
    assert(pthread_create(&thread->thread, NULL, host_thread_entry, thread) == 0);
    thread->started = true;
}

void furi_thread_join(FuriThread* thread) {
    if(thread->started) {
        assert(pthread_join(thread->thread, NULL) == 0);
        thread->started = false;
    }
}

void furi_thread_free(FuriThread* thread) {
    assert(!thread->started);
    free(thread);
}

void furi_delay_ms(uint32_t duration_ms) {
    const struct timespec duration = {
        .tv_sec = duration_ms / 1000U,
        .tv_nsec = (long)(duration_ms % 1000U) * 1000000L,
    };
    nanosleep(&duration, NULL);
}

FuriEventFlag* furi_event_flag_alloc(void) {
    FuriEventFlag* event = calloc(1, sizeof(FuriEventFlag));
    assert(event);
    assert(pthread_mutex_init(&event->mutex, NULL) == 0);
    assert(pthread_cond_init(&event->condition, NULL) == 0);
    return event;
}

void furi_event_flag_free(FuriEventFlag* event) {
    assert(pthread_cond_destroy(&event->condition) == 0);
    assert(pthread_mutex_destroy(&event->mutex) == 0);
    free(event);
}

uint32_t furi_event_flag_set(FuriEventFlag* event, uint32_t flags) {
    assert(pthread_mutex_lock(&event->mutex) == 0);
    event->flags |= flags;
    const uint32_t result = event->flags;
    assert(pthread_cond_broadcast(&event->condition) == 0);
    assert(pthread_mutex_unlock(&event->mutex) == 0);
    return result;
}

uint32_t furi_event_flag_clear(FuriEventFlag* event, uint32_t flags) {
    assert(pthread_mutex_lock(&event->mutex) == 0);
    event->flags &= ~flags;
    const uint32_t result = event->flags;
    assert(pthread_mutex_unlock(&event->mutex) == 0);
    return result;
}

uint32_t furi_event_flag_wait(
    FuriEventFlag* event,
    uint32_t flags,
    uint32_t options,
    uint32_t timeout_ms) {
    UNUSED(options);
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += timeout_ms / 1000U;
    deadline.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if(deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    assert(pthread_mutex_lock(&event->mutex) == 0);
    while((event->flags & flags) == 0U) {
        const int result = pthread_cond_timedwait(&event->condition, &event->mutex, &deadline);
        if(result == ETIMEDOUT) {
            assert(pthread_mutex_unlock(&event->mutex) == 0);
            return FuriFlagErrorTimeout;
        }
        assert(result == 0);
    }
    const uint32_t result = event->flags & flags;
    event->flags &= ~result;
    assert(pthread_mutex_unlock(&event->mutex) == 0);
    return result;
}

FuriStreamBuffer* furi_stream_buffer_alloc(size_t size, size_t trigger_level) {
    UNUSED(trigger_level);
    FuriStreamBuffer* stream = calloc(1, sizeof(FuriStreamBuffer));
    assert(stream);
    stream->capacity = size;
    return stream;
}

void furi_stream_buffer_free(FuriStreamBuffer* stream) {
    free(stream);
}

void furi_stream_buffer_reset(FuriStreamBuffer* stream) {
    stream->available = 0;
}

size_t furi_stream_buffer_send(
    FuriStreamBuffer* stream,
    const void* data,
    size_t size,
    uint32_t timeout_ms) {
    UNUSED(data);
    UNUSED(timeout_ms);
    if(stream->available + size > stream->capacity) return 0;
    stream->available += size;
    return size;
}

size_t furi_stream_buffer_receive(
    FuriStreamBuffer* stream,
    void* data,
    size_t size,
    uint32_t timeout_ms) {
    UNUSED(data);
    UNUSED(timeout_ms);
    if(stream->available < size) return 0;
    stream->available -= size;
    return size;
}

size_t furi_stream_buffer_bytes_available(FuriStreamBuffer* stream) {
    return stream->available;
}

size_t furi_stream_buffer_spaces_available(FuriStreamBuffer* stream) {
    return stream->capacity - stream->available;
}

FuriString* furi_string_alloc(void) {
    FuriString* string = calloc(1, sizeof(FuriString));
    assert(string);
    string->value = strdup("");
    assert(string->value);
    return string;
}

void furi_string_free(FuriString* string) {
    free(string->value);
    free(string);
}

void furi_string_set(FuriString* string, const char* value) {
    char* copy = strdup(value);
    assert(copy);
    free(string->value);
    string->value = copy;
}

const char* furi_string_get_cstr(const FuriString* string) {
    return string->value;
}

void furi_string_printf(FuriString* string, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    assert(needed >= 0);
    char* value = malloc((size_t)needed + 1U);
    assert(value);
    va_start(args, format);
    assert(vsnprintf(value, (size_t)needed + 1U, format, args) == needed);
    va_end(args);
    free(string->value);
    string->value = value;
}

void furi_string_trim(FuriString* string) {
    UNUSED(string);
}

Storage* furi_record_open(const char* record) {
    UNUSED(record);
    return &storage;
}

void furi_record_close(const char* record) {
    UNUSED(record);
}

FlipperFormat* flipper_format_file_alloc(Storage* storage_instance) {
    UNUSED(storage_instance);
    FlipperFormat* format = calloc(1, sizeof(FlipperFormat));
    assert(format);
    return format;
}

void flipper_format_free(FlipperFormat* format) {
    free(format);
}

bool flipper_format_file_open_existing(FlipperFormat* format, const char* path) {
    UNUSED(format);
    UNUSED(path);
    open_count++;
    return open_count != fail_open_number;
}

bool flipper_format_read_string(
    FlipperFormat* format,
    const char* key,
    FuriString* value) {
    UNUSED(format);
    UNUSED(key);
    furi_string_set(value, "RAW");
    return true;
}

void flipper_format_file_close(FlipperFormat* format) {
    UNUSED(format);
}

Stream* flipper_format_get_raw_stream(FlipperFormat* format) {
    return &format->stream;
}

size_t stream_size(Stream* stream) {
    UNUSED(stream);
    return 100U;
}

size_t stream_tell(Stream* stream) {
    return stream->offset;
}

bool stream_seek(Stream* stream, int32_t offset, int origin) {
    UNUSED(origin);
    stream->offset += (size_t)offset;
    return true;
}

bool stream_read_line(Stream* stream, FuriString* value) {
    if(stream->returned_line) return false;
    stream->returned_line = true;
    stream->offset += 20U;
    furi_string_set(value, "RAW_Data: 100, -100");
    return true;
}

int strint_to_int32(const char* input, char** end, int32_t* value, int base) {
    char* parsed_end;
    const long parsed = strtol(input, &parsed_end, base);
    if(parsed_end == input) return 1;
    *value = (int32_t)parsed;
    *end = parsed_end;
    return StrintParseNoError;
}

const SubGhzDevice* subghz_devices_get_by_name(const char* name) {
    return strcmp(name, "internal") == 0 ? &radio : NULL;
}

bool subghz_devices_is_async_complete_tx(const SubGhzDevice* device) {
    assert(device == &radio);
    async_tx_checks++;
    return false;
}

static uint64_t monotonic_milliseconds(void) {
    struct timespec timestamp;
    assert(clock_gettime(CLOCK_MONOTONIC, &timestamp) == 0);
    return (uint64_t)timestamp.tv_sec * 1000U + (uint64_t)timestamp.tv_nsec / 1000000U;
}

static void simulate_initial_metadata_parse(const char* path) {
    Storage* metadata_storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* metadata = flipper_format_file_alloc(metadata_storage);
    FuriString* protocol = furi_string_alloc();
    assert(flipper_format_file_open_existing(metadata, path));
    assert(flipper_format_read_string(metadata, "Protocol", protocol));
    flipper_format_file_close(metadata);
    furi_string_free(protocol);
    flipper_format_free(metadata);
    furi_record_close(RECORD_STORAGE);
}

int main(void) {
    const char* path = "/ext/subghz/test.sub";
    open_count = 0;
    fail_open_number = 2;
    async_tx_checks = 0;

    simulate_initial_metadata_parse(path);
    SubGhzFileEncoderWorker* worker = subghz_file_encoder_worker_alloc();
    const uint64_t start_ms = monotonic_milliseconds();
    const bool started = subghz_file_encoder_worker_start(worker, path, "internal");
    const uint64_t elapsed_ms = monotonic_milliseconds() - start_ms;

    if(started && subghz_file_encoder_worker_is_running(worker)) {
        subghz_file_encoder_worker_stop(worker);
    }
    subghz_file_encoder_worker_free(worker);

    if(open_count != 2U) {
        fprintf(stderr, "expected metadata open plus worker reopen, got %u opens\n", open_count);
        return 1;
    }
    if(started) {
        fprintf(stderr, "worker reported success after injected reopen failure\n");
        return 1;
    }
    if(elapsed_ms > 500U) {
        fprintf(stderr, "startup failure took %llu ms\n", (unsigned long long)elapsed_ms);
        return 1;
    }
    if(async_tx_checks != 0U) {
        fprintf(stderr, "failed startup entered TX completion wait %u times\n", async_tx_checks);
        return 1;
    }

    return 0;
}
