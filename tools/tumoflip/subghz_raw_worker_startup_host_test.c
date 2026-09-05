#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdatomic.h>
#include <time.h>

#include <flipper_format/flipper_format.h>
#include <lib/subghz/subghz_file_encoder_worker.h>

struct FuriThread {
    pthread_t thread;
    FuriThreadCallback callback;
    void* context;
    bool started;
};

struct FuriStreamBuffer {
    pthread_mutex_t mutex;
    int32_t* values;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
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
    bool is_open;
};

struct SubGhzDevice {
    int unused;
};

static Storage storage;
static SubGhzDevice radio;
static pthread_mutex_t storage_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t storage_condition = PTHREAD_COND_INITIALIZER;
static unsigned open_handles;
static unsigned open_count;
static unsigned fail_open_number;
static unsigned async_tx_checks;
static bool async_tx_complete;
static atomic_uint callback_count;

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

FuriStreamBuffer* furi_stream_buffer_alloc(size_t size, size_t trigger_level) {
    UNUSED(trigger_level);
    FuriStreamBuffer* stream = calloc(1, sizeof(FuriStreamBuffer));
    assert(stream);
    assert(size % sizeof(int32_t) == 0U);
    stream->capacity = size / sizeof(int32_t);
    stream->values = calloc(stream->capacity, sizeof(int32_t));
    assert(stream->values);
    assert(pthread_mutex_init(&stream->mutex, NULL) == 0);
    return stream;
}

void furi_stream_buffer_free(FuriStreamBuffer* stream) {
    assert(pthread_mutex_destroy(&stream->mutex) == 0);
    free(stream->values);
    free(stream);
}

void furi_stream_buffer_reset(FuriStreamBuffer* stream) {
    assert(pthread_mutex_lock(&stream->mutex) == 0);
    stream->head = 0;
    stream->tail = 0;
    stream->count = 0;
    assert(pthread_mutex_unlock(&stream->mutex) == 0);
}

size_t furi_stream_buffer_send(
    FuriStreamBuffer* stream,
    const void* data,
    size_t size,
    uint32_t timeout_ms) {
    UNUSED(timeout_ms);
    assert(size == sizeof(int32_t));
    assert(pthread_mutex_lock(&stream->mutex) == 0);
    if(stream->count == stream->capacity) {
        assert(pthread_mutex_unlock(&stream->mutex) == 0);
        return 0;
    }
    stream->values[stream->tail] = *(const int32_t*)data;
    stream->tail = (stream->tail + 1U) % stream->capacity;
    stream->count++;
    assert(pthread_mutex_unlock(&stream->mutex) == 0);
    return size;
}

size_t furi_stream_buffer_receive(
    FuriStreamBuffer* stream,
    void* data,
    size_t size,
    uint32_t timeout_ms) {
    UNUSED(timeout_ms);
    assert(size == sizeof(int32_t));
    assert(pthread_mutex_lock(&stream->mutex) == 0);
    if(stream->count == 0U) {
        assert(pthread_mutex_unlock(&stream->mutex) == 0);
        return 0;
    }
    *(int32_t*)data = stream->values[stream->head];
    stream->head = (stream->head + 1U) % stream->capacity;
    stream->count--;
    assert(pthread_mutex_unlock(&stream->mutex) == 0);
    return size;
}

size_t furi_stream_buffer_bytes_available(FuriStreamBuffer* stream) {
    assert(pthread_mutex_lock(&stream->mutex) == 0);
    const size_t available = stream->count * sizeof(int32_t);
    assert(pthread_mutex_unlock(&stream->mutex) == 0);
    return available;
}

size_t furi_stream_buffer_spaces_available(FuriStreamBuffer* stream) {
    assert(pthread_mutex_lock(&stream->mutex) == 0);
    const size_t available = (stream->capacity - stream->count) * sizeof(int32_t);
    assert(pthread_mutex_unlock(&stream->mutex) == 0);
    return available;
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
    assert(!format->is_open);
    free(format);
}

bool flipper_format_file_open_existing(FlipperFormat* format, const char* path) {
    UNUSED(path);
    assert(pthread_mutex_lock(&storage_mutex) == 0);
    open_count++;
    if(open_count == fail_open_number) {
        assert(pthread_mutex_unlock(&storage_mutex) == 0);
        return false;
    }
    while(open_handles != 0U) {
        assert(pthread_cond_wait(&storage_condition, &storage_mutex) == 0);
    }
    format->is_open = true;
    open_handles++;
    assert(pthread_mutex_unlock(&storage_mutex) == 0);
    return true;
}

bool flipper_format_read_string(FlipperFormat* format, const char* key, FuriString* value) {
    UNUSED(format);
    UNUSED(key);
    furi_string_set(value, "RAW");
    return true;
}

void flipper_format_file_close(FlipperFormat* format) {
    assert(pthread_mutex_lock(&storage_mutex) == 0);
    if(format->is_open) {
        assert(open_handles == 1U);
        format->is_open = false;
        open_handles--;
        assert(pthread_cond_broadcast(&storage_condition) == 0);
    }
    assert(pthread_mutex_unlock(&storage_mutex) == 0);
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
    return async_tx_complete;
}

static uint64_t monotonic_milliseconds(void) {
    struct timespec timestamp;
    assert(clock_gettime(CLOCK_MONOTONIC, &timestamp) == 0);
    return (uint64_t)timestamp.tv_sec * 1000U + (uint64_t)timestamp.tv_nsec / 1000000U;
}

static FlipperFormat* open_initial_metadata(const char* path) {
    Storage* metadata_storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* metadata = flipper_format_file_alloc(metadata_storage);
    FuriString* protocol = furi_string_alloc();
    assert(flipper_format_file_open_existing(metadata, path));
    assert(flipper_format_read_string(metadata, "Protocol", protocol));
    furi_string_free(protocol);
    furi_record_close(RECORD_STORAGE);
    return metadata;
}

static void close_initial_metadata(FlipperFormat* metadata) {
    flipper_format_file_close(metadata);
    flipper_format_free(metadata);
}

static void worker_end_callback(void* context) {
    assert(context == &callback_count);
    atomic_fetch_add(&callback_count, 1U);
}

static void reset_fake_storage(void) {
    assert(open_handles == 0U);
    open_count = 0;
    fail_open_number = 0;
    async_tx_checks = 0;
    async_tx_complete = false;
    atomic_store(&callback_count, 0U);
}

static int test_open_handle_is_released_after_async_start(void) {
    const char* path = "/ext/subghz/test.sub";
    reset_fake_storage();
    async_tx_complete = true;

    FlipperFormat* metadata = open_initial_metadata(path);
    SubGhzFileEncoderWorker* worker = subghz_file_encoder_worker_alloc();
    subghz_file_encoder_worker_callback_end(worker, worker_end_callback, &callback_count);
    const uint64_t start_ms = monotonic_milliseconds();
    const bool started = subghz_file_encoder_worker_start(worker, path, "internal");
    const uint64_t elapsed_ms = monotonic_milliseconds() - start_ms;
    if(!started || elapsed_ms > 100U) {
        fprintf(
            stderr,
            "start must return while metadata handle is open: started=%u elapsed=%llu ms\n",
            started,
            (unsigned long long)elapsed_ms);
        return 1;
    }
    close_initial_metadata(metadata);

    LevelDuration output[3] = {0};
    size_t output_count = 0;
    const uint64_t deadline_ms = monotonic_milliseconds() + 500U;
    while(output_count < 3U && monotonic_milliseconds() < deadline_ms) {
        const LevelDuration duration = subghz_file_encoder_worker_get_level_duration(worker);
        if(!duration.wait) output[output_count++] = duration;
        if(duration.wait) furi_delay_ms(1);
    }
    while(atomic_load(&callback_count) == 0U && monotonic_milliseconds() < deadline_ms) {
        furi_delay_ms(1);
    }
    if(subghz_file_encoder_worker_is_running(worker)) {
        subghz_file_encoder_worker_stop(worker);
    }
    subghz_file_encoder_worker_free(worker);

    if(open_count != 2U) {
        fprintf(stderr, "normal path expected two opens, got %u\n", open_count);
        return 1;
    }
    if(output_count != 3U || output[0].level != 1 || output[0].duration != 100U ||
       output[1].level != 0 || output[1].duration != 100U || !output[2].reset) {
        fprintf(stderr, "normal path did not preserve pulse, pulse, EOF reset\n");
        return 1;
    }
    if(async_tx_checks == 0U) {
        fprintf(stderr, "normal path skipped TX completion check\n");
        return 1;
    }
    if(atomic_load(&callback_count) == 0U) {
        fprintf(stderr, "normal path skipped EOF callback\n");
        return 1;
    }
    if(open_handles != 0U) {
        fprintf(stderr, "normal path leaked an open handle\n");
        return 1;
    }

    return 0;
}

static int test_hard_reopen_failure_is_terminal(void) {
    const char* path = "/ext/subghz/test.sub";
    reset_fake_storage();

    FlipperFormat* metadata = open_initial_metadata(path);
    close_initial_metadata(metadata);
    fail_open_number = 2;
    SubGhzFileEncoderWorker* worker = subghz_file_encoder_worker_alloc();
    if(!subghz_file_encoder_worker_start(worker, path, "internal")) {
        fprintf(stderr, "async start synchronously rejected a worker reopen failure\n");
        subghz_file_encoder_worker_free(worker);
        return 1;
    }

    LevelDuration terminal = {.wait = true};
    unsigned non_wait_values = 0;
    const uint64_t deadline_ms = monotonic_milliseconds() + 500U;
    while(monotonic_milliseconds() < deadline_ms) {
        const LevelDuration duration = subghz_file_encoder_worker_get_level_duration(worker);
        if(!duration.wait) {
            terminal = duration;
            non_wait_values++;
            break;
        }
        if(duration.wait) furi_delay_ms(1);
    }

    subghz_file_encoder_worker_callback_end(worker, worker_end_callback, &callback_count);
    while(atomic_load(&callback_count) == 0U && monotonic_milliseconds() < deadline_ms) {
        furi_delay_ms(1);
    }
    for(unsigned i = 0; i < 20U; i++) {
        const LevelDuration duration = subghz_file_encoder_worker_get_level_duration(worker);
        if(!duration.wait) non_wait_values++;
        furi_delay_ms(1);
    }
    if(subghz_file_encoder_worker_is_running(worker)) {
        subghz_file_encoder_worker_stop(worker);
    }
    subghz_file_encoder_worker_free(worker);

    if(open_count != 2U) {
        fprintf(stderr, "failure path expected two opens, got %u\n", open_count);
        return 1;
    }
    if(non_wait_values != 1U || !terminal.reset) {
        fprintf(stderr, "failure path must emit exactly one EOF reset and no pulses\n");
        return 1;
    }
    if(async_tx_checks != 0U) {
        fprintf(stderr, "failure path entered TX completion wait %u times\n", async_tx_checks);
        return 1;
    }
    if(atomic_load(&callback_count) == 0U) {
        fprintf(stderr, "failure path ignored a late callback registration\n");
        return 1;
    }
    if(open_handles != 0U) {
        fprintf(stderr, "failure path leaked an open handle\n");
        return 1;
    }

    return 0;
}

static int test_unknown_radio_is_rejected_immediately(void) {
    const char* path = "/ext/subghz/test.sub";
    reset_fake_storage();
    SubGhzFileEncoderWorker* worker = subghz_file_encoder_worker_alloc();
    if(subghz_file_encoder_worker_start(worker, path, "missing")) {
        fprintf(stderr, "unknown radio was accepted\n");
        return 1;
    }
    if(subghz_file_encoder_worker_is_running(worker)) {
        fprintf(stderr, "unknown radio started a worker thread\n");
        return 1;
    }
    subghz_file_encoder_worker_free(worker);
    if(open_count != 0U || open_handles != 0U) {
        fprintf(stderr, "unknown radio touched storage\n");
        return 1;
    }
    return 0;
}

int main(void) {
    if(test_open_handle_is_released_after_async_start() != 0) return 1;
    if(test_hard_reopen_failure_is_terminal() != 0) return 1;
    if(test_unknown_radio_is_rejected_immediately() != 0) return 1;
    return 0;
}
