#include "uhf_runtime.h"
#include <furi_hal_rtc.h>
#include <string.h>

static void uhf_runtime_send(UhfRuntime* runtime, uint8_t command) {
    uint8_t bytes[6];
    const size_t size = uhf_inventory_command(command, bytes);
    if(runtime->serial && size) {
        furi_hal_serial_tx(runtime->serial, bytes, size);
        furi_hal_serial_tx_wait_complete(runtime->serial);
    }
}

static void uhf_runtime_rx(FuriHalSerialHandle* serial, FuriHalSerialRxEvent event, void* context) {
    UhfRuntime* runtime = context;
    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
                FuriHalSerialRxEventOverrunError | FuriHalSerialRxEventParityError)) {
        runtime->overflow = true;
    }
    if(event & FuriHalSerialRxEventData) {
        const uint8_t byte = furi_hal_serial_async_rx(serial);
        if(furi_stream_buffer_send(runtime->stream, &byte, 1, 0) != 1U) runtime->overflow = true;
    }
}

void uhf_runtime_stop(UhfRuntime* runtime) {
    if(runtime && runtime->running) {
        uhf_runtime_send(runtime, 0x8C);
        runtime->running = false;
    }
}

void uhf_runtime_disconnect(UhfRuntime* runtime) {
    if(!runtime) return;
    uhf_runtime_stop(runtime);
    if(runtime->serial) {
        if(runtime->initialized) {
            furi_hal_serial_async_rx_stop(runtime->serial);
            furi_hal_serial_deinit(runtime->serial);
            runtime->initialized = false;
        }
        furi_hal_serial_control_release(runtime->serial);
        runtime->serial = NULL;
    }
    if(runtime->stream) {
        furi_stream_buffer_free(runtime->stream);
        runtime->stream = NULL;
    }
    if(runtime->expansion) {
        expansion_enable(runtime->expansion); // respects the user's saved setting
        furi_record_close(RECORD_EXPANSION);
        runtime->expansion = NULL;
    }
    if(runtime->inventory) runtime->inventory->identified = false;
}

bool uhf_runtime_connect(UhfRuntime* runtime, UhfInventory* inventory) {
    if(!runtime || !inventory || runtime->serial) return false;
    memset(runtime, 0, sizeof(*runtime));
    runtime->inventory = inventory;
    inventory->identified = false;
    inventory->used = 0;
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    if(expansion_is_connected(expansion)) {
        furi_record_close(RECORD_EXPANSION);
        return false; // never disconnect an already attached expansion session
    }
    runtime->expansion = expansion;
    expansion_disable(expansion);
    runtime->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    runtime->stream = furi_stream_buffer_alloc(4096U, 1U);
    if(!runtime->serial || !runtime->stream) {
        uhf_runtime_disconnect(runtime);
        return false;
    }
    furi_hal_serial_init(runtime->serial, 115200U);
    runtime->initialized = true;
    furi_hal_serial_async_rx_start(runtime->serial, uhf_runtime_rx, runtime, true);
    runtime->last_rx = furi_get_tick();
    runtime->last_poll = runtime->last_rx;
    uhf_runtime_send(runtime, 0x72); // version probe only; never starts RF
    return true;
}

bool uhf_runtime_start(UhfRuntime* runtime) {
    if(!runtime || !runtime->serial || !runtime->inventory->identified || runtime->running ||
       furi_get_tick() - runtime->last_reply > 3000U) return false;
    runtime->started = runtime->last_poll = furi_get_tick();
    runtime->completed = runtime->inventory->completed;
    runtime->timed_out = false;
    runtime->running = true;
    uhf_runtime_send(runtime, 0x89);
    return true;
}

void uhf_runtime_tick(UhfRuntime* runtime) {
    if(!runtime || !runtime->serial) return;
    const uint32_t now = furi_get_tick();
    if(runtime->overflow) {
        FURI_CRITICAL_ENTER();
        runtime->overflow = false;
        FURI_CRITICAL_EXIT();
        uhf_inventory_discontinuity(runtime->inventory);
        furi_stream_buffer_reset(runtime->stream);
        uhf_runtime_stop(runtime); // incomplete UART data is not a valid RF session
    }
    uint8_t bytes[64];
    for(unsigned budget = 0; budget < 16U; budget++) {
        const size_t size = furi_stream_buffer_receive(runtime->stream, bytes, sizeof(bytes), 0);
        if(!size) break;
        if(now - runtime->last_rx > 200U && runtime->inventory->used)
            uhf_inventory_discontinuity(runtime->inventory);
        runtime->last_rx = now;
        for(size_t i = 0; i < size; i++) {
            const uint32_t completed = runtime->inventory->completed;
            if(uhf_inventory_feed(runtime->inventory, bytes[i], furi_hal_rtc_get_timestamp()) ||
               completed != runtime->inventory->completed) runtime->last_reply = now;
        }
    }
    if(runtime->running) {
        if(now - runtime->started >= 60000U || now - runtime->last_reply > 3000U) {
            uhf_runtime_stop(runtime);
            runtime->timed_out = true;
        } else if(runtime->inventory->completed != runtime->completed &&
                  now - runtime->last_poll >= 1000U) {
            runtime->completed = runtime->inventory->completed;
            runtime->last_poll = now;
            uhf_runtime_send(runtime, 0x89);
        }
    } else if(now - runtime->last_poll >= 2000U) {
        runtime->last_poll = now;
        if(now - runtime->last_reply > 3000U) runtime->inventory->identified = false;
        uhf_runtime_send(runtime, 0x72);
    }
}
