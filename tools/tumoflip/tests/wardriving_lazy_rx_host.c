#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#define furi_assert assert
#define SUBGHZ_KEYSTORE_DIR_NAME "system"
#define SUBGHZ_KEYSTORE_DIR_USER_NAME "user"
#define SUBGHZ_ALUTECH_AT_4N_DIR_NAME "alutech"
#define SUBGHZ_NICE_FLOR_S_DIR_NAME "nice"

typedef int SubGhzProtocolFlag;
typedef void (*SubGhzReceiverCallback)(void*, void*);
typedef void (*SubGhzWorkerOverrunCallback)(void*);
typedef void (*SubGhzWorkerPairCallback)(void*, bool, unsigned);
typedef struct { int unused; } SubGhzEnvironment;
typedef struct {
    SubGhzProtocolFlag filter;
    SubGhzReceiverCallback callback;
    void* context;
} SubGhzReceiver;
typedef struct {
    bool running;
    SubGhzWorkerOverrunCallback overrun;
    SubGhzWorkerPairCallback pair;
    void* context;
} SubGhzWorker;
enum { SubGhzTxRxStateSleep, SubGhzTxRxStateIDLE, SubGhzTxRxStateRx, SubGhzTxRxStateTx };
enum { SubGhzRadioBrokerStateCleaningUp, SubGhzRadioBrokerStateInitialized };
typedef struct {
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    void* transmitter;
    void* decoder_result;
    bool is_database_loaded;
    int txrx_state;
    void* radio_device;
    SubGhzProtocolFlag filter;
    SubGhzReceiverCallback rx_callback;
    void* rx_callback_context;
} SubGhzWarDrivingTxRx;

static int subghz_protocol_registry;
static int environments, receivers, workers, loads;
static bool database_ok = true, producer_running;
static SubGhzEnvironment* subghz_environment_alloc(void) {
    environments++;
    return calloc(1, sizeof(SubGhzEnvironment));
}
static void subghz_environment_free(SubGhzEnvironment* p) {
    assert(p && receivers == 0 && workers == 0);
    environments--; free(p);
}
static bool subghz_environment_load_keystore(SubGhzEnvironment* p, const char* file) {
    assert(p && file); loads++; return database_ok;
}
static void subghz_environment_set_alutech_at_4n_rainbow_table_file_name(SubGhzEnvironment* p, const char* f) { assert(p && f); }
static void subghz_environment_set_nice_flor_s_rainbow_table_file_name(SubGhzEnvironment* p, const char* f) { assert(p && f); }
static void subghz_environment_set_protocol_registry(SubGhzEnvironment* p, const void* r) { assert(p && r); }
static SubGhzReceiver* subghz_receiver_alloc_init(SubGhzEnvironment* p) {
    assert(p); receivers++; return calloc(1, sizeof(SubGhzReceiver));
}
static void subghz_receiver_free(SubGhzReceiver* p) {
    assert(p && workers == 0); receivers--; free(p);
}
static void subghz_receiver_set_filter(SubGhzReceiver* p, SubGhzProtocolFlag f) { assert(p); p->filter = f; }
static void subghz_receiver_set_rx_callback(SubGhzReceiver* p, SubGhzReceiverCallback c, void* context) { assert(p); p->callback = c; p->context = context; }
static void subghz_receiver_reset(void* p) { assert(p); }
static void subghz_receiver_decode(void* p, bool level, unsigned duration) { (void)level; (void)duration; assert(p); }
static SubGhzWorker* subghz_worker_alloc(void) { workers++; return calloc(1, sizeof(SubGhzWorker)); }
static void subghz_worker_free(SubGhzWorker* p) { assert(p && !p->running && !producer_running); workers--; free(p); }
static bool subghz_worker_is_running(SubGhzWorker* p) { assert(p); return p->running; }
static void subghz_worker_stop(SubGhzWorker* p) { assert(p && p->running && !producer_running); p->running = false; }
static void subghz_worker_set_overrun_callback(SubGhzWorker* p, SubGhzWorkerOverrunCallback c) { p->overrun = c; }
static void subghz_worker_set_pair_callback(SubGhzWorker* p, SubGhzWorkerPairCallback c) { p->pair = c; }
static void subghz_worker_set_context(SubGhzWorker* p, void* c) { p->context = c; }
static void subghz_devices_stop_async_rx(void* device) { (void)device; assert(producer_running); producer_running = false; }
static void subghz_devices_idle(void* device) { (void)device; assert(!producer_running); }
static void subghz_wardriving_txrx_speaker_off(SubGhzWarDrivingTxRx* p) { assert(p); }
static void subghz_wardriving_txrx_radio_state(SubGhzWarDrivingTxRx* p, int state) { assert(p); (void)state; }

/* ACTUAL_FUNCTIONS */

static void receive(void* a, void* b) { (void)a; (void)b; }

int main(void) {
    SubGhzWarDrivingTxRx app = {0};
    subghz_wardriving_txrx_rx_pipeline_release(&app);
    // Real database parsing may fail; do not replace it with a file-presence check.
    database_ok = false;
    assert(!subghz_wardriving_txrx_is_database_loaded(&app));
    assert(environments == 1 && receivers == 0 && workers == 0);
    subghz_wardriving_txrx_rx_pipeline_release(&app);
    assert(environments == 0);
    database_ok = true;
    assert(subghz_wardriving_txrx_is_database_loaded(&app));
    subghz_wardriving_txrx_rx_pipeline_release(&app);
    for(int i = 0; i < 100; i++) {
        subghz_wardriving_txrx_receiver_set_filter(&app, i + 1);
        subghz_wardriving_txrx_set_rx_callback(&app, receive, &app);
        assert(!app.receiver && !app.worker && !app.environment);
        SubGhzReceiver* rx = subghz_wardriving_txrx_get_receiver(&app);
        int previous_loads = loads;
        assert(subghz_wardriving_txrx_get_receiver(&app) == rx);
        assert(loads == previous_loads && environments == 1 && receivers == 1 && workers == 1);
        assert(rx->filter == i + 1 && rx->callback == receive && rx->context == &app);
        assert(app.worker->context == rx && app.worker->pair && app.worker->overrun);
        app.worker->overrun(app.worker->context);
        app.worker->pair(app.worker->context, true, 100);
        app.decoder_result = rx;
        app.txrx_state = SubGhzTxRxStateTx;
        subghz_wardriving_txrx_rx_pipeline_release(&app);
        assert(app.receiver == rx);
        app.txrx_state = SubGhzTxRxStateRx;
        producer_running = app.worker->running = true;
        subghz_wardriving_txrx_rx_pipeline_release(&app);
        assert(app.receiver == rx && producer_running);
        subghz_wardriving_txrx_rx_end(&app);
        assert(!producer_running && !app.worker->running);
        assert(app.txrx_state == SubGhzTxRxStateIDLE);
        subghz_wardriving_txrx_set_rx_callback(&app, NULL, NULL);
        assert(!rx->callback && !rx->context);
        subghz_wardriving_txrx_rx_pipeline_release(&app);
        subghz_wardriving_txrx_rx_pipeline_release(&app);
        assert(!app.decoder_result && !app.environment && !app.receiver && !app.worker);
        assert(!environments && !receivers && !workers);
    }
    return 0;
}
