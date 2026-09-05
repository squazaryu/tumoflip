/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Tumoflip contributors
 *
 * Quac adapter for bounded, read-only, non-secure Picopass emulation.
 * Protocol and licensing provenance: picopass/PROVENANCE.md.
 */
#include "action_i.h"
#include "quac.h"
#include "picopass/quac_picopass.h"

#include <furi.h>
#include <furi_hal_nfc.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <storage/storage.h>
#include <toolbox/bit_buffer.h>

#define QUAC_PICOPASS_CANCEL_FLAG (1U << 0)
#define QUAC_PICOPASS_ERROR_FLAG  (1U << 1)
// Picopass nominal VCD EOF-to-first-modulation delay: 330 us at 13.56 MHz.
// The firmware HAL applies its ISO15693 listener compensation internally.
#define QUAC_PICOPASS_FDT_FC      4475U

typedef struct {
    App* app;
    Nfc* nfc;
    BitBuffer* tx_buffer;
    QuacPicopassSession* session;
    FuriEventFlag* event_flag;
    FuriPubSub* input_events;
    FuriPubSubSubscription* input_subscription;
    bool nfc_configured;
    bool nfc_started;
} QuacPicopassAdapter;

static void action_picopass_secure_clear(void* memory, size_t size) {
    volatile uint8_t* bytes = memory;
    while(size--)
        *bytes++ = 0;
}

static QuacPicopassParseResult
    action_picopass_load(void* context, const char* path, QuacPicopassCredential* credential) {
    QuacPicopassAdapter* adapter = context;
    File* file = storage_file_alloc(adapter->app->storage);
    uint8_t* file_data = NULL;
    size_t file_data_size = 0;
    QuacPicopassParseResult result = QuacPicopassParseInvalid;

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        const uint64_t file_size_u64 = storage_file_size(file);
        if(file_size_u64 == 0 || file_size_u64 > QUAC_PICOPASS_MAX_FILE_SIZE) break;
        const size_t file_size = (size_t)file_size_u64;
        file_data_size = file_size;
        file_data = malloc(file_size);
        if(!file_data) break;
        if(storage_file_read(file, file_data, file_size) != file_size) break;
        result = quac_picopass_parse((const char*)file_data, file_size, credential);
    } while(false);

    if(file_data) {
        action_picopass_secure_clear(file_data, file_data_size);
        free(file_data);
    }
    storage_file_close(file);
    storage_file_free(file);
    return result;
}

static void action_picopass_input_callback(const void* value, void* context) {
    furi_assert(value);
    furi_assert(context);
    QuacPicopassAdapter* adapter = context;
    const InputEvent* event = value;
    if(event->key == InputKeyBack && event->type == InputTypePress) {
        furi_event_flag_set(adapter->event_flag, QUAC_PICOPASS_CANCEL_FLAG);
    }
}

static NfcCommand action_picopass_nfc_callback(NfcEvent event, void* context) {
    QuacPicopassAdapter* adapter = context;
    if(event.type == NfcEventTypeFieldOff) {
        quac_picopass_session_init(adapter->session, adapter->session->credential);
        return NfcCommandContinue;
    }
    if(event.type != NfcEventTypeRxEnd) return NfcCommandContinue;

    const BitBuffer* rx_buffer = event.data.buffer;
    if(bit_buffer_has_partial_byte(rx_buffer)) return NfcCommandContinue;

    uint8_t response[QUAC_PICOPASS_MAX_RESPONSE_LEN] = {0};
    size_t response_size = 0;
    bool response_is_sof = false;
    const QuacPicopassFrameResult result = quac_picopass_process_frame(
        adapter->session,
        bit_buffer_get_data(rx_buffer),
        bit_buffer_get_size_bytes(rx_buffer),
        response,
        sizeof(response),
        &response_size,
        &response_is_sof);

    NfcError error = NfcErrorNone;
    if(result == QuacPicopassFrameResponse) {
        bit_buffer_reset(adapter->tx_buffer);
        bit_buffer_copy_bytes(adapter->tx_buffer, response, response_size);
        error = nfc_listener_tx(adapter->nfc, adapter->tx_buffer);
        bit_buffer_reset(adapter->tx_buffer);
    } else if(result == QuacPicopassFrameSof && response_is_sof) {
        error = nfc_iso15693_listener_tx_sof(adapter->nfc);
    } else if(result == QuacPicopassFrameError) {
        error = NfcErrorInternal;
    }
    action_picopass_secure_clear(response, sizeof(response));

    if(error != NfcErrorNone) {
        furi_event_flag_set(adapter->event_flag, QUAC_PICOPASS_ERROR_FLAG);
        return NfcCommandStop;
    }
    return NfcCommandContinue;
}

static bool action_picopass_start(void* context, QuacPicopassSession* session) {
    QuacPicopassAdapter* adapter = context;
    adapter->session = session;
    adapter->event_flag = furi_event_flag_alloc();
    adapter->input_events = furi_record_open(RECORD_INPUT_EVENTS);
    adapter->input_subscription =
        furi_pubsub_subscribe(adapter->input_events, action_picopass_input_callback, adapter);
    adapter->tx_buffer = bit_buffer_alloc(QUAC_PICOPASS_MAX_RESPONSE_LEN);
    adapter->nfc = nfc_alloc();
    nfc_config(adapter->nfc, NfcModeListener, NfcTechIso15693);
    adapter->nfc_configured = true;
    if(furi_hal_nfc_iso15693_force_1outof4() != FuriHalNfcErrorNone) return false;
    nfc_set_fdt_listen_fc(adapter->nfc, QUAC_PICOPASS_FDT_FC);
    nfc_start(adapter->nfc, action_picopass_nfc_callback, adapter);
    adapter->nfc_started = true;
    return true;
}

static QuacPicopassWaitResult action_picopass_wait(void* context, uint32_t duration_ms) {
    QuacPicopassAdapter* adapter = context;
    const uint32_t flags = furi_event_flag_wait(
        adapter->event_flag,
        QUAC_PICOPASS_CANCEL_FLAG | QUAC_PICOPASS_ERROR_FLAG,
        FuriFlagWaitAny,
        duration_ms);
    if(flags == (uint32_t)FuriFlagErrorTimeout) return QuacPicopassWaitComplete;
    if(flags & FuriFlagError) return QuacPicopassWaitError;
    if(flags & QUAC_PICOPASS_CANCEL_FLAG) return QuacPicopassWaitCancelled;
    return QuacPicopassWaitError;
}

static void action_picopass_stop(void* context) {
    QuacPicopassAdapter* adapter = context;
    if(adapter->nfc_started) {
        nfc_stop(adapter->nfc);
    } else if(adapter->nfc_configured) {
        furi_hal_nfc_reset_mode();
        furi_hal_nfc_low_power_mode_start();
    }
    adapter->nfc_started = false;
    adapter->nfc_configured = false;
    if(adapter->nfc) {
        nfc_free(adapter->nfc);
        adapter->nfc = NULL;
    }
    if(adapter->tx_buffer) {
        bit_buffer_reset(adapter->tx_buffer);
        bit_buffer_free(adapter->tx_buffer);
        adapter->tx_buffer = NULL;
    }
    if(adapter->input_subscription) {
        furi_pubsub_unsubscribe(adapter->input_events, adapter->input_subscription);
        adapter->input_subscription = NULL;
    }
    if(adapter->input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        adapter->input_events = NULL;
    }
    if(adapter->event_flag) {
        furi_event_flag_free(adapter->event_flag);
        adapter->event_flag = NULL;
    }
    adapter->session = NULL;
}

void action_picopass_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;
    QuacPicopassAdapter adapter = {.app = app};
    const QuacPicopassRuntimeOps ops = {
        .context = &adapter,
        .load = action_picopass_load,
        .start = action_picopass_start,
        .wait = action_picopass_wait,
        .stop = action_picopass_stop,
    };

    const QuacPicopassRunResult result = quac_picopass_run(
        &ops, furi_string_get_cstr(action_path), app->settings.picopass_duration);
    if(result == QuacPicopassRunCancelled) {
        app->action_cancelled = true;
        app->suppress_next_back = true;
    } else if(result == QuacPicopassRunLoadError) {
        ACTION_SET_ERROR("Picopass: Invalid credential file");
    } else if(result == QuacPicopassRunUnsupportedSecure) {
        ACTION_SET_ERROR("Picopass: Unsupported secure card");
    } else if(result == QuacPicopassRunStartError) {
        ACTION_SET_ERROR("Picopass: NFC listener start failed");
    } else if(result == QuacPicopassRunWaitError) {
        ACTION_SET_ERROR("Picopass: NFC listener failed");
    } else if(result == QuacPicopassRunInvalidDuration) {
        ACTION_SET_ERROR("Picopass: Invalid duration");
    }
}
