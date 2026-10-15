#include "rx_link_view.h"
#include <gui/elements.h>
#include <stdio.h>

static void rx_link_row(Canvas* canvas, int y, const char* label, uint32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    canvas_draw_str(canvas, 2, y, label);
    canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, text);
}

void rx_link_draw(Canvas* canvas, const RxLinkViewModel* m) {
    static const char* const frequencies[] = {"315.00 MHz", "433.92 MHz", "868.35 MHz"};
    static const char* const modulations[] = {"AM650", "FM238"};
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Tumo RX Link");
    canvas_set_font(canvas, FontSecondary);
    const bool loss = m->local_drops || m->snapshot.lost_batches || m->snapshot.rejected_batches ||
                      m->uart_errors;
    const char* status = m->phase == RxUiLive    ? (loss ? "LOSS" : "LIVE") :
                         m->phase == RxUiWaiting ? "WAIT" :
                         m->phase == RxUiError   ? "ERR" :
                         m->phase == RxUiStopped ? "STOP" :
                                                   "IDLE";
    canvas_draw_rframe(canvas, 95, 0, 33, 12, 2);
    canvas_draw_str_aligned(canvas, 111, 9, AlignCenter, AlignBottom, status);
    canvas_draw_line(canvas, 0, 14, 127, 14);
    if(m->phase == RxUiIdle) {
        canvas_draw_str(canvas, 2, 25, "Module One / Receive only");
        canvas_draw_str(canvas, 2, 38, "Up/Down:");
        canvas_draw_str_aligned(
            canvas, 126, 38, AlignRight, AlignBottom, frequencies[m->frequency % 3]);
        canvas_draw_str(canvas, 2, 50, "Left/Right:");
        canvas_draw_str_aligned(
            canvas, 126, 50, AlignRight, AlignBottom, modulations[m->modulation % 2]);
        elements_button_center(canvas, "Start");
    } else if(m->phase == RxUiWaiting) {
        canvas_draw_str(canvas, 2, 28, "Checking ESP32 firmware");
        canvas_draw_str(canvas, 2, 41, "Radio capture not started");
        elements_button_center(canvas, "Cancel");
    } else if(m->phase == RxUiError) {
        static const char* const errors[] = {
            "Stopped",
            "UART is busy",
            "Radio is busy",
            "External CC1101 missing",
            "RX Link firmware required",
            "ESP32 link lost",
            "CC1101 disconnected"};
        canvas_draw_str(canvas, 2, 28, errors[m->error <= RxErrorRadioLost ? m->error : 0]);
        canvas_draw_str(canvas, 2, 41, "Check Module One, retry");
        elements_button_center(canvas, "Retry");
    } else {
        if(m->page == 0) {
            char line[24];
            snprintf(
                line,
                sizeof(line),
                "%s %s",
                frequencies[m->frequency % 3],
                modulations[m->modulation % 2]);
            canvas_draw_str(canvas, 2, 24, line);
            uint32_t peak = 1;
            for(unsigned i = 0; i < RX_LINK_BINS; i++)
                if(m->snapshot.bins[i] > peak) peak = m->snapshot.bins[i];
            canvas_draw_line(canvas, 7, 44, 119, 44);
            for(unsigned i = 0; i < RX_LINK_BINS; i++) {
                unsigned height = (unsigned)(((uint64_t)m->snapshot.bins[i] * 16) / peak);
                if(height) canvas_draw_box(canvas, 8 + i * 7, 44 - height, 5, height);
            }
            snprintf(line, sizeof(line), "Mean %lu us", (unsigned long)m->snapshot.mean);
            canvas_draw_str(canvas, 2, 54, line);
        } else if(m->page == 1) {
            rx_link_row(canvas, 24, "Samples", m->snapshot.count);
            rx_link_row(canvas, 34, "Min us", m->snapshot.minimum);
            rx_link_row(canvas, 44, "Max us", m->snapshot.maximum);
            rx_link_row(canvas, 54, "High /1000", m->snapshot.high_permille);
        } else {
            rx_link_row(canvas, 24, "Dropped edges", m->local_drops);
            rx_link_row(canvas, 34, "Lost batches", m->snapshot.lost_batches);
            rx_link_row(canvas, 44, "Rejected", m->snapshot.rejected_batches);
            rx_link_row(canvas, 54, "UART errors", m->uart_errors);
        }
        char page[8];
        snprintf(page, sizeof(page), "%u/3", m->page + 1);
        canvas_draw_str(canvas, 111, 63, page);
        elements_button_center(canvas, m->phase == RxUiLive ? "Stop" : "Restart");
    }
}
