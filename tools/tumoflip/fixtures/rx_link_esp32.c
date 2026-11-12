#include "rx_link.h"
#include <assert.h>
#include <setjmp.h>
#include <string.h>

enum {
    UART_NUM_0,
    UART_DATA_8_BITS,
    UART_PARITY_DISABLE,
    UART_STOP_BITS_1,
    UART_HW_FLOWCTRL_DISABLE,
    UART_SCLK_DEFAULT,
    UART_PIN_NO_CHANGE
};
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
#define ESP_ERROR_CHECK(x) assert((x) == 0)
#define pdMS_TO_TICKS(x)   (x)
static jmp_buf done;
static unsigned step, reply_count;
static const uint32_t times[] = {0, 10, 120, 130, 140, 2200, 2210, 2220};
static uint32_t current_time;
static int uart_param_config(int port, const uart_config_t* cfg) {
    assert(port == UART_NUM_0 && cfg->baud_rate == 230400);
    return 0;
}
static int uart_set_pin(int port, int tx, int rx, int rts, int cts) {
    assert(
        port == UART_NUM_0 && tx == 1 && rx == 3 && rts == UART_PIN_NO_CHANGE &&
        cts == UART_PIN_NO_CHANGE);
    return 0;
}
static int uart_driver_install(int port, int rx, int tx, int n, void* q, int flags) {
    assert(port == UART_NUM_0 && rx == 2048 && tx == 0 && n == 0 && !q && flags == 0);
    return 0;
}
static uint64_t esp_timer_get_time(void) {
    return (uint64_t)current_time * 1000;
}
static int uart_read_bytes(int port, void* data, size_t capacity, int timeout) {
    assert(port == UART_NUM_0 && capacity >= RX_LINK_FRAME_MAX && timeout == 20);
    if(step == 8) longjmp(done, 1);
    current_time = times[step];
    RxLinkFrame f = {0};
    if(step == 0 || step == 6) {
        f.type = RxLinkHello;
        f.length = 8;
        memcpy(f.data, "RXA1", 4);
        rx_link_write32(f.data + 4, step == 0 ? 1 : 2);
    } else if(step == 1 || step == 3) {
        f.type = RxLinkEdges;
        f.length = 12;
        rx_link_write32(f.data, 1);
        rx_link_write32(f.data + 8, 100);
    } else {
        f.type = RxLinkQuery;
        f.length = 4;
        rx_link_write32(f.data, step == 7 ? 2 : 1);
    }
    size_t count = rx_link_encode(&f, data, capacity);
    if(step == 1) count = 10;
    step++;
    return (int)count;
}
static int uart_write_bytes(int port, const void* data, size_t length) {
    assert(port == UART_NUM_0);
    RxLinkParser p = {0};
    RxLinkFrame f;
    bool complete = false;
    for(size_t i = 0; i < length; i++)
        complete = rx_link_feed(&p, ((const uint8_t*)data)[i], &f);
    assert(complete);
    reply_count++;
    if(step == 1 || step == 7)
        assert(f.type == RxLinkReady);
    else {
        RxLinkSnapshot s;
        assert(rx_link_snapshot_decode(&f, step == 8 ? 2 : 1, &s));
        if(step == 3)
            assert(s.count == 0 && s.rejected_batches == 1);
        else if(step == 5)
            assert(s.count == 1 && s.rejected_batches == 1);
        else if(step == 8)
            assert(s.count == 0 && s.rejected_batches == 0);
        else
            assert(!"unexpected response after an expired session");
    }
    return (int)length;
}

/* PRODUCTION */

int main(void) {
    if(!setjmp(done)) app_main();
    assert(step == 8 && reply_count == 5);
    return 0;
}
