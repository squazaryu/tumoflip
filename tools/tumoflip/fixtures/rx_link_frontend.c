#include "rx_link.h"
#include <assert.h>
#include <string.h>

typedef enum {
    RxUiIdle,
    RxUiWaiting,
    RxUiLive,
    RxUiStopped,
    RxUiError
} RxUiPhase;
typedef enum {
    RxErrorNone,
    RxErrorUart,
    RxErrorRadio,
    RxErrorModule,
    RxErrorFirmware,
    RxErrorDisconnected,
    RxErrorRadioLost
} RxLinkError;
typedef struct {
    void* device;
} SubGhzAppRadio;
typedef struct {
    bool serial_initialized, serial_receiving, radio_receiving;
    void *serial, *edges, *uart_rx;
    SubGhzAppRadio radio;
    RxLinkClient client;
    RxLinkParser parser;
    struct {
        RxUiPhase phase;
        RxLinkError error;
        RxLinkSnapshot snapshot;
    } state;
    uint32_t dropped_edges, last_query, last_uart;
    uint16_t sequence;
} RxLinkApp;
enum {
    FuriStatusOk = 0
};
static bool connected = true, fail_start;
static unsigned starts, stops, uart_stops, releases, sends, edge_count, edge_sends, hellos;
static uint8_t queued[RX_LINK_FRAME_MAX];
static size_t queued_size;
static void rx_link_stop_io(RxLinkApp*);
static void rx_link_fail(RxLinkApp*, RxLinkError);
static void subghz_devices_stop_async_rx(void* d) {
    assert(d);
    stops++;
}
static void rx_link_send(RxLinkApp* app, const RxLinkFrame* f) {
    assert(app->serial_initialized);
    if(f->type == RxLinkStop) assert(!app->radio_receiving);
    if(f->type == RxLinkEdges) {
        assert(app->radio_receiving);
        edge_sends++;
    }
    sends++;
}
static void furi_hal_serial_async_rx_stop(void* d) {
    assert(d);
    uart_stops++;
}
static void furi_hal_serial_deinit(void* d) {
    assert(d);
    assert(uart_stops);
}
static void furi_hal_serial_control_release(void* d) {
    assert(d);
}
static void subghz_app_radio_release(SubGhzAppRadio* r) {
    if(r->device) {
        releases++;
        r->device = NULL;
    }
}
static bool subghz_app_radio_is_connected(SubGhzAppRadio* r) {
    return r->device && connected;
}
static void rx_link_hello(RxLinkApp* app) {
    assert(app->client.waiting);
    hellos++;
}
static void rx_link_begin_radio(RxLinkApp* app) {
    assert(app->client.connected && !app->client.waiting);
    if(fail_start) {
        rx_link_fail(app, RxErrorRadio);
        return;
    }
    starts++;
    app->radio_receiving = true;
    app->state.phase = RxUiLive;
}
static size_t furi_stream_buffer_receive(void* queue, void* data, size_t limit, unsigned timeout) {
    (void)queue;
    assert(timeout == 0);
    assert(queued_size <= limit);
    size_t n = queued_size;
    memcpy(data, queued, n);
    queued_size = 0;
    return n;
}
static int furi_message_queue_get(void* queue, void* value, unsigned timeout) {
    (void)queue;
    assert(timeout == 0);
    if(edge_count) {
        *(uint32_t*)value = 100;
        edge_count--;
        return FuriStatusOk;
    }
    return 1;
}

/* PRODUCTION */

static void prepare(RxLinkApp* app, uint32_t session, uint32_t now) {
    memset(app, 0, sizeof(*app));
    app->serial_initialized = app->serial_receiving = true;
    app->serial = (void*)1;
    app->radio.device = (void*)2;
    app->state.phase = RxUiWaiting;
    rx_link_client_begin(&app->client, session, now);
}
static void ready(uint32_t session, uint8_t board) {
    RxLinkFrame f = {.type = RxLinkReady, .length = 9};
    memcpy(f.data, "RXA1", 4);
    rx_link_write32(f.data + 4, session);
    f.data[8] = board;
    queued_size = rx_link_encode(&f, queued, sizeof(queued));
}
int main(void) {
    RxLinkApp app;
    prepare(&app, 123, 0);
    edge_count = 2;
    ready(999, 1);
    rx_link_poll(&app, 10);
    assert(!starts && !app.radio_receiving);
    ready(123, 2);
    rx_link_poll(&app, 20);
    assert(!starts && !app.radio_receiving);
    assert(edge_count == 2 && !edge_sends);
    ready(123, 1);
    rx_link_poll(&app, 30);
    assert(starts == 1 && app.radio_receiving);
    assert(edge_count == 0 && edge_sends == 1);
    ready(123, 1);
    rx_link_poll(&app, 40);
    assert(starts == 1);
    rx_link_poll(&app, 2030);
    assert(stops == 1 && uart_stops == 1 && releases == 1);
    assert(app.state.phase == RxUiError && app.state.error == RxErrorDisconnected);
    unsigned before = sends;
    rx_link_stop_io(&app);
    assert(stops == 1 && uart_stops == 1 && releases == 1 && sends == before);
    queued_size = 0;
    prepare(&app, 456, 3000);
    fail_start = true;
    ready(456, 1);
    rx_link_poll(&app, 3100);
    assert(app.state.error == RxErrorRadio && !app.client.connected);
    assert(
        starts == 1 && releases == 2); // A failed begin is not overwritten by another poll error.
    fail_start = false;
    prepare(&app, 789, 4000);
    ready(789, 1);
    rx_link_poll(&app, 4010);
    assert(starts == 2);
    connected = false;
    rx_link_poll(&app, 4300);
    assert(app.state.error == RxErrorRadioLost && stops == 2 && releases == 3);
    connected = true;
    prepare(&app, 222, 5000);
    rx_link_poll(&app, 5500);
    assert(hellos == 1);
    rx_link_poll(&app, 10000);
    assert(app.state.error == RxErrorFirmware && starts == 2);
    assert(stops == 2 && releases == 4);
    return 0;
}
