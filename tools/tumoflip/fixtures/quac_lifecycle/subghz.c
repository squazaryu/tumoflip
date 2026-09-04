#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define FURI_LOG_I(...)         ((void)0)
#define FURI_LOG_E(...)         ((void)0)
#define FURI_LOG_W(...)         ((void)0)
#define furi_assert(x)          assert(x)
#define ACTION_SET_ERROR(...)   furi_string_printf(error, __VA_ARGS__)
#define SUBGHZ_KEY_FILE_TYPE    "Flipper SubGhz Key File"
#define SUBGHZ_RAW_FILE_TYPE    "Flipper SubGhz RAW File"
#define SUBGHZ_KEY_FILE_VERSION 1
#define FuriFlagWaitAll         1
#define FuriWaitForever         UINT32_MAX
#define REQUIRE(x)                                                \
    do {                                                          \
        if(!(x)) {                                                \
            fprintf(stderr, "SubGHz invariant failed: %s\n", #x); \
            return 1;                                             \
        }                                                         \
    } while(0)
typedef struct {
    char text[256];
} FuriString;
typedef struct {
    size_t bytes;
} Stream;
typedef struct {
    Stream stream;
} FlipperFormat;
typedef int SubGhzSetting;
typedef int FuriThread;
typedef enum {
    SubGhzTxRxStartTxStateOk,
    SubGhzTxRxStartTxStateErrorParserOthers
} SubGhzTxRxStartTxState;
typedef enum {
    SubGhzProtocolStatusOk,
    SubGhzProtocolStatusError
} SubGhzProtocolStatus;
typedef struct {
    void* storage;
    struct {
        uint32_t subghz_duration;
    } settings;
} App;
typedef struct {
    FlipperFormat data;
    bool active;
    void (*save)(void*);
    void* context;
} SubGhzTxRx;
typedef struct {
    App* app;
    SubGhzTxRx* txrx;
    const FuriString* file_path;
} SubGhzNeedSaveContext;
static const char* mode;
static unsigned starts, waits, delays, callback_installs, saves, cleaned, live;
static uint32_t delayed_ms;
static bool raw;
static bool is(const char* value) {
    return !strcmp(mode, value);
}
static FuriString* furi_string_alloc(void) {
    live++;
    return calloc(1, sizeof(FuriString));
}
static void furi_string_free(FuriString* s) {
    live--;
    free(s);
}
static const char* furi_string_get_cstr(const FuriString* s) {
    return s->text;
}
static void furi_string_set_str(FuriString* s, const char* text) {
    snprintf(s->text, sizeof(s->text), "%s", text);
}
static void furi_string_set(FuriString* s, FuriString* value) {
    *s = *value;
}
static void furi_string_printf(FuriString* s, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s->text, sizeof(s->text), fmt, args);
    va_end(args);
}
static FlipperFormat* flipper_format_file_alloc(void* storage) {
    live++;
    return calloc(1, sizeof(FlipperFormat));
}
static void flipper_format_free(FlipperFormat* f) {
    live--;
    free(f);
}
static void flipper_format_file_close(FlipperFormat* f) {
}
static bool flipper_format_file_open_existing(FlipperFormat* f, const char* path) {
    f->stream.bytes = 64;
    return !is("open");
}
static Stream* flipper_format_get_raw_stream(FlipperFormat* f) {
    return &f->stream;
}
static void stream_clean(Stream* s) {
    s->bytes = 0;
}
static size_t stream_size(Stream* s) {
    return s->bytes;
}
static size_t stream_copy_full(Stream* from, Stream* to) {
    return to->bytes = is("copy") ? 8 : from->bytes;
}
static bool flipper_format_read_header(FlipperFormat* f, FuriString* t, uint32_t* version) {
    furi_string_set_str(t, raw ? SUBGHZ_RAW_FILE_TYPE : SUBGHZ_KEY_FILE_TYPE);
    *version = is("version") ? 9 : 1;
    return !is("header");
}
static bool flipper_format_read_uint32(FlipperFormat* f, const char* key, uint32_t* v, size_t n) {
    *v = 433920000;
    return !is("default-frequency");
}
static bool flipper_format_read_string(FlipperFormat* f, const char* key, FuriString* t) {
    if(!strcmp(key, "Protocol")) {
        furi_string_set_str(t, raw ? "RAW" : "Princeton");
        return !is("protocol");
    }
    furi_string_set_str(t, is("custom") || is("valid-custom") ? "CUSTOM" : "AM650");
    return true;
}
static SubGhzTxRx* subghz_txrx_alloc(void) {
    live++;
    return calloc(1, sizeof(SubGhzTxRx));
}
static void subghz_txrx_free(SubGhzTxRx* t) {
    assert(!t->active);
    cleaned++;
    live--;
    free(t);
}
static FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* t) {
    return &t->data;
}
static void action_subghz_need_save_callback(void* context) {
    SubGhzNeedSaveContext* save = context;
    assert(save->app && save->txrx && save->file_path);
    saves++;
}
static void subghz_txrx_set_need_save_callback(SubGhzTxRx* t, void (*cb)(void*), void* ctx) {
    t->save = cb;
    t->context = ctx;
}
static SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* t) {
    static int setting;
    return &setting;
}
static uint32_t subghz_setting_get_default_frequency(SubGhzSetting* s) {
    return 433920000;
}
static bool subghz_txrx_radio_device_is_frequecy_valid(SubGhzTxRx* t, uint32_t f) {
    return !is("frequency");
}
static const char* subghz_txrx_get_preset_name(SubGhzTxRx* t, const char* n) {
    return is("preset") ? "" : (is("custom") || is("valid-custom") ? "CUSTOM" : "AM650");
}
static void subghz_setting_delete_custom_preset(SubGhzSetting* s, const char* name) {
}
static bool
    subghz_setting_load_custom_preset(SubGhzSetting* s, const char* name, FlipperFormat* f) {
    return !is("custom");
}
static size_t subghz_setting_get_inx_preset_by_name(SubGhzSetting* s, const char* name) {
    return 0;
}
static uint8_t* subghz_setting_get_preset_data(SubGhzSetting* s, size_t i) {
    static uint8_t data;
    return &data;
}
static size_t subghz_setting_get_preset_data_size(SubGhzSetting* s, size_t i) {
    return 1;
}
static void
    subghz_txrx_set_preset(SubGhzTxRx* t, const char* name, uint32_t f, uint8_t* d, size_t n) {
    assert(f == 433920000);
}
static const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* t) {
    return "internal";
}
static void
    subghz_protocol_raw_gen_fff_data(FlipperFormat* f, const char* path, const char* radio) {
    f->stream.bytes = 64;
}
static bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* t, const char* protocol) {
    return !is("decoder");
}
static void* subghz_txrx_get_decoder(SubGhzTxRx* t) {
    return t;
}
static SubGhzProtocolStatus
    subghz_protocol_decoder_base_deserialize(void* decoder, FlipperFormat* f) {
    return is("deserialize") ? SubGhzProtocolStatusError : SubGhzProtocolStatusOk;
}
static SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* t, FlipperFormat* f) {
    starts++;
    t->active = !is("start-raw") && !is("start-parsed");
    return t->active ? SubGhzTxRxStartTxStateOk : SubGhzTxRxStartTxStateErrorParserOthers;
}
static FuriThread* furi_thread_get_current(void) {
    static int thread;
    return &thread;
}
static FuriThread* furi_thread_get_id(FuriThread* t) {
    return t;
}
static void furi_thread_flags_set(FuriThread* t, uint32_t flags) {
}
static void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* t,
    void (*cb)(void*),
    void* ctx) {
    assert(t->active);
    callback_installs++;
    cb(ctx);
}
static uint32_t furi_thread_flags_wait(uint32_t f, uint32_t options, uint32_t timeout) {
    waits++;
    return 0;
}
static void furi_delay_ms(uint32_t ms) {
    delays++;
    delayed_ms = ms;
}
static void subghz_txrx_stop(SubGhzTxRx* t) {
    if(t->active) {
        if(!raw) t->save(t->context);
        t->active = false;
    }
}

/* @PRODUCTION@ */

int main(int argc, char** argv) {
    assert(argc == 2);
    mode = argv[1];
    raw = strstr(mode, "raw") != NULL;
    App app = {.settings.subghz_duration = 1234};
    FuriString path = {.text = "/ext/subghz/owned.sub"}, error = {0};
    action_subghz_tx(&app, &path, &error);
    bool valid = is("raw") || is("parsed") || is("default-frequency") || is("valid-custom");
    bool rejected = is("start-raw") || is("start-parsed");
    REQUIRE(starts == (valid || rejected ? 1U : 0U));
    REQUIRE(waits == (valid && raw ? 1U : 0U));
    REQUIRE(callback_installs == (valid && raw ? 1U : 0U));
    REQUIRE(delays == (valid && !raw ? 1U : 0U));
    REQUIRE(saves == (valid && !raw ? 1U : 0U));
    REQUIRE(!delays || delayed_ms == 1234);
    REQUIRE((error.text[0] == 0) == valid);
    REQUIRE(cleaned == 1 && live == 0);
    return 0;
}
