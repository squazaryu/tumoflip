#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define furi_assert(x) assert(x)
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr, "TX start invariant failed: %s\n", #x); return 1; } } while(0)
typedef struct { const char* text; } FuriString;
typedef int FlipperFormat;
typedef enum { SubGhzTxRxStateSleep, SubGhzTxRxStateIDLE, SubGhzTxRxStateTx } State;
typedef enum { SubGhzTxRxStartTxStateOk, SubGhzTxRxStartTxStateErrorParserOthers, SubGhzTxRxStartTxStateErrorOnlyRx } SubGhzTxRxStartTxState;
typedef enum { SubGhzProtocolStatusOk, SubGhzProtocolStatusError } SubGhzProtocolStatus;
typedef struct { FuriString* name; uint32_t frequency; } SubGhzRadioPreset;
typedef struct { SubGhzRadioPreset* preset; void* transmitter; void* environment; void* radio_device; void* setting; State txrx_state; } SubGhzTxRx;
enum { SubGhzRadioBrokerStateAsyncTx };
static const char* mode;
static unsigned async_calls, frees, broker_async, saves, strings;
static bool is(const char* name) { return !strcmp(mode, name); }
static FuriString* furi_string_alloc(void) { strings++; return calloc(1, sizeof(FuriString)); }
static void furi_string_free(FuriString* s) { strings--; free(s); }
static const char* furi_string_get_cstr(FuriString* s) { return s->text; }
static bool flipper_format_rewind(FlipperFormat* f) { return !is("rewind"); }
static bool flipper_format_read_string(FlipperFormat* f, const char* k, FuriString* s) { s->text = "Princeton"; return !is("protocol"); }
static bool flipper_format_insert_or_update_uint32(FlipperFormat* f, const char* k, uint32_t* v, size_t n) { return !is("repeat"); }
static void subghz_txrx_stop(SubGhzTxRx* t) { if(t->txrx_state == SubGhzTxRxStateTx) saves++; }
static void* subghz_transmitter_alloc_init(void* env, const char* protocol) { return is("no-encoder") ? NULL : malloc(1); }
static SubGhzProtocolStatus subghz_transmitter_deserialize(void* t, FlipperFormat* f) { return is("deserialize") ? SubGhzProtocolStatusError : SubGhzProtocolStatusOk; }
static void* subghz_setting_get_preset_data_by_name(void* s, const char* n) { return s; }
static void subghz_txrx_begin(SubGhzTxRx* t, void* data) { t->txrx_state = SubGhzTxRxStateIDLE; }
static bool subghz_txrx_tx(SubGhzTxRx* t, uint32_t f) { if(is("only-rx")) return false; t->txrx_state = SubGhzTxRxStateTx; return true; }
static void subghz_transmitter_yield(void) {}
static bool subghz_devices_start_async_tx(void* device, void (*cb)(void), void* ctx) { async_calls++; return !is("async-failure"); }
static void subghz_txrx_radio_state(SubGhzTxRx* t, int state) { broker_async++; }
static void subghz_transmitter_free(void* t) { if(t) { frees++; free(t); } }
static void subghz_txrx_idle(SubGhzTxRx* t) { t->txrx_state = SubGhzTxRxStateIDLE; }
static void subghz_txrx_speaker_off(SubGhzTxRx* t) {}

@PRODUCTION@

int main(int argc, char** argv) {
    assert(argc == 2); mode = argv[1];
    FuriString name = {.text = is("preset") ? "" : "AM650"};
    SubGhzRadioPreset preset = {.name = &name, .frequency = is("frequency") ? 0 : 433920000};
    SubGhzTxRx txrx = {.preset = &preset, .txrx_state = SubGhzTxRxStateSleep};
    FlipperFormat data = 0;
    SubGhzTxRxStartTxState result = subghz_txrx_tx_start(&txrx, &data);
    bool valid = is("success");
    REQUIRE((result == SubGhzTxRxStartTxStateOk) == valid);
    REQUIRE(broker_async == (valid ? 1U : 0U));
    REQUIRE(async_calls == (valid || is("async-failure") ? 1U : 0U));
    REQUIRE(valid || txrx.txrx_state != SubGhzTxRxStateTx);
    REQUIRE(saves == 0 && strings == 0);
    if(valid) subghz_transmitter_free(txrx.transmitter);
    REQUIRE(frees == (is("no-encoder") || is("rewind") || is("protocol") || is("repeat") ? 0U : 1U));
    return 0;
}
