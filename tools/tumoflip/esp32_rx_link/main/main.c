#include "rx_link.h"
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "This experimental profile is for Module One ESP32, not ESP32-C5"
#endif

// Module One v6.1: UART0 TX GPIO1 -> Flipper RX14, RX GPIO3 <- Flipper TX13.
// No Wi-Fi/BLE, flash writes, RF transmission or emulation subsystem is started.
void app_main(void) {
    const uart_config_t config = {
        .baud_rate = RX_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0));
    RxLinkBackend backend = {0};
    RxLinkParser parser = {0};
    uint32_t last_byte = 0;
    uint32_t last_request = 0;
    while(true) {
        uint8_t bytes[256];
        const int received = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if((uint32_t)(now - last_byte) > 100) parser.used = parser.expected = 0;
        if(received > 0) last_byte = now;
        if(backend.active && (uint32_t)(now - last_request) > 2000) backend.active = false;
        for(int i = 0; i < received; i++) {
            RxLinkFrame frame, reply;
            if(!rx_link_feed(&parser, bytes[i], &frame)) continue;
            bool respond = rx_link_backend_handle(&backend, &frame, &reply);
            if(respond || (backend.active && frame.type == RxLinkEdges && frame.length >= 4 &&
                           rx_link_read32(frame.data) == backend.session))
                last_request = now;
            if(respond) {
                uint8_t wire[RX_LINK_FRAME_MAX];
                size_t length = rx_link_encode(&reply, wire, sizeof(wire));
                if(length) uart_write_bytes(UART_NUM_0, wire, length);
            }
        }
    }
}
