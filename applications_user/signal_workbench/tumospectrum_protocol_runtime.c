#include "tumospectrum_protocol_runtime.h"

#include <furi.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <toolbox/level_duration.h>

#include <stdlib.h>
#include <string.h>

#define TUMOSPECTRUM_PROTOCOL_OWNER           "tumospectrum"
#define TUMOSPECTRUM_PROTOCOL_ACQUIRE_TIMEOUT 500U
#define TUMOSPECTRUM_PROTOCOL_STREAM_ITEMS    768U
#define TUMOSPECTRUM_PROTOCOL_FRAME_GAP_US    8000U
#define TUMOSPECTRUM_PROTOCOL_MIN_DURATION_US 40U
#define TUMOSPECTRUM_PROTOCOL_MAX_DURATION_US 1000000U

struct TumoSpectrumProtocolRuntime {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* device;
    FuriStreamBuffer* stream;
    ProtocolProfilePackage package;
    TumoSpectrumProtocolObservationCallback callback;
    void* callback_context;
    int32_t pulses[ProtocolProfileMaximumCapturePulses];
    size_t pulse_count;
    uint32_t current_api;
    uint32_t match_count;
    uint64_t previous_value;
    float rssi;
    ProtocolProfileStatus decode_status;
    TumoSpectrumProtocolRadioStatus radio_status;
    volatile bool overrun;
    bool has_previous;
    bool devices_initialized;
    bool device_begun;
    bool listening;
};

static void tumospectrum_protocol_rx_callback(bool level, uint32_t duration, void* context) {
    TumoSpectrumProtocolRuntime* runtime = context;
    LevelDuration event = level_duration_make(level, duration);
    if(runtime->overrun) {
        runtime->overrun = false;
        event = level_duration_reset();
    }
    if(furi_stream_buffer_send(runtime->stream, &event, sizeof(event), 0U) != sizeof(event)) {
        runtime->overrun = true;
    }
}

static void tumospectrum_protocol_runtime_reset_capture(TumoSpectrumProtocolRuntime* runtime) {
    runtime->pulse_count = 0U;
}

static void tumospectrum_protocol_runtime_decode(TumoSpectrumProtocolRuntime* runtime) {
    if(runtime->pulse_count == 0U) return;

    ProtocolProfileDecodeResult decode = {0};
    runtime->decode_status = protocol_profile_decode(
        &runtime->package.profile,
        runtime->current_api,
        runtime->pulses,
        runtime->pulse_count,
        &decode);
    if(runtime->decode_status == ProtocolProfileStatusOk) {
        const uint64_t changed_mask = runtime->has_previous ?
                                          (runtime->previous_value ^ decode.value) &
                                              runtime->package.profile.variable_mask :
                                          0U;
        runtime->previous_value = decode.value;
        runtime->has_previous = true;
        runtime->match_count++;
        if(runtime->callback != NULL) {
            const TumoSpectrumProtocolObservation observation = {
                .decode = decode,
                .changed_mask = changed_mask,
                .match_count = runtime->match_count,
            };
            runtime->callback(&observation, runtime->callback_context);
        }
    }
    tumospectrum_protocol_runtime_reset_capture(runtime);
}

TumoSpectrumProtocolRuntime* tumospectrum_protocol_runtime_alloc(void) {
    TumoSpectrumProtocolRuntime* runtime = malloc(sizeof(*runtime));
    if(runtime == NULL) return NULL;
    memset(runtime, 0, sizeof(*runtime));
    runtime->stream = furi_stream_buffer_alloc(
        sizeof(LevelDuration) * TUMOSPECTRUM_PROTOCOL_STREAM_ITEMS, sizeof(LevelDuration));
    if(runtime->stream == NULL) {
        free(runtime);
        return NULL;
    }
    runtime->radio_status = TumoSpectrumProtocolRadioIdle;
    runtime->decode_status = ProtocolProfileStatusNoMatch;
    return runtime;
}

void tumospectrum_protocol_runtime_stop(TumoSpectrumProtocolRuntime* runtime) {
    if(runtime == NULL) return;
    if(runtime->listening) {
        subghz_radio_broker_set_state(
            runtime->broker, &runtime->lease, SubGhzRadioBrokerStateCleaningUp);
        subghz_devices_stop_async_rx(runtime->device);
        runtime->listening = false;
    }
    if(runtime->device_begun) {
        subghz_devices_sleep(runtime->device);
        subghz_devices_end(runtime->device);
        runtime->device_begun = false;
    }
    if(runtime->devices_initialized) {
        subghz_devices_deinit();
        runtime->devices_initialized = false;
    }
    if(runtime->broker != NULL) {
        subghz_radio_broker_release(runtime->broker, &runtime->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        runtime->broker = NULL;
    }
    runtime->device = NULL;
    runtime->overrun = false;
    runtime->rssi = -127.0f;
    runtime->radio_status = TumoSpectrumProtocolRadioIdle;
    tumospectrum_protocol_runtime_reset_capture(runtime);
    furi_stream_buffer_reset(runtime->stream);
}

void tumospectrum_protocol_runtime_free(TumoSpectrumProtocolRuntime* runtime) {
    if(runtime == NULL) return;
    tumospectrum_protocol_runtime_stop(runtime);
    furi_stream_buffer_free(runtime->stream);
    free(runtime);
}

TumoSpectrumProtocolRadioStatus tumospectrum_protocol_runtime_start(
    TumoSpectrumProtocolRuntime* runtime,
    const ProtocolProfilePackage* package,
    uint32_t current_api,
    TumoSpectrumProtocolObservationCallback callback,
    void* callback_context) {
    if(runtime == NULL || package == NULL || package->status != ProtocolProfileStatusOk ||
       protocol_profile_validate(&package->profile, current_api) != ProtocolProfileStatusOk ||
       package->profile.frequency_hz == 0U) {
        if(runtime != NULL) runtime->radio_status = TumoSpectrumProtocolRadioInvalidFrequency;
        return TumoSpectrumProtocolRadioInvalidFrequency;
    }

    tumospectrum_protocol_runtime_stop(runtime);
    runtime->package = *package;
    runtime->current_api = current_api;
    runtime->callback = callback;
    runtime->callback_context = callback_context;
    runtime->decode_status = ProtocolProfileStatusNoMatch;
    runtime->match_count = 0U;
    runtime->has_previous = false;
    runtime->rssi = -127.0f;

    runtime->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!subghz_radio_broker_acquire(
           runtime->broker,
           TUMOSPECTRUM_PROTOCOL_OWNER,
           TUMOSPECTRUM_PROTOCOL_ACQUIRE_TIMEOUT,
           &runtime->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        runtime->broker = NULL;
        runtime->radio_status = TumoSpectrumProtocolRadioBusy;
        return runtime->radio_status;
    }

    subghz_devices_init();
    runtime->devices_initialized = true;
    runtime->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(runtime->device == NULL ||
       !subghz_devices_is_frequency_valid(runtime->device, package->profile.frequency_hz) ||
       !subghz_devices_begin(runtime->device)) {
        const TumoSpectrumProtocolRadioStatus failure =
            runtime->device != NULL ? TumoSpectrumProtocolRadioInvalidFrequency :
                                      TumoSpectrumProtocolRadioError;
        tumospectrum_protocol_runtime_stop(runtime);
        runtime->radio_status = failure;
        return failure;
    }
    runtime->device_begun = true;
    subghz_radio_broker_set_selected_device(
        runtime->broker, &runtime->lease, SubGhzRadioBrokerDeviceInternal);
    subghz_radio_broker_set_state(
        runtime->broker, &runtime->lease, SubGhzRadioBrokerStateInitialized);
    subghz_devices_reset(runtime->device);
    subghz_devices_load_preset(runtime->device, FuriHalSubGhzPresetOok650Async, NULL);
    if(subghz_devices_set_frequency(runtime->device, package->profile.frequency_hz) == 0U) {
        tumospectrum_protocol_runtime_stop(runtime);
        runtime->radio_status = TumoSpectrumProtocolRadioInvalidFrequency;
        return TumoSpectrumProtocolRadioInvalidFrequency;
    }
    subghz_devices_flush_rx(runtime->device);
    subghz_devices_set_rx(runtime->device);
    subghz_radio_broker_set_state(runtime->broker, &runtime->lease, SubGhzRadioBrokerStateRx);
    subghz_devices_start_async_rx(runtime->device, tumospectrum_protocol_rx_callback, runtime);
    runtime->listening = true;
    runtime->radio_status = TumoSpectrumProtocolRadioListening;
    subghz_radio_broker_set_state(runtime->broker, &runtime->lease, SubGhzRadioBrokerStateAsyncRx);
    return runtime->radio_status;
}

void tumospectrum_protocol_runtime_tick(TumoSpectrumProtocolRuntime* runtime) {
    if(runtime == NULL || !runtime->listening) return;
    bool received = false;
    LevelDuration event;
    while(furi_stream_buffer_receive(runtime->stream, &event, sizeof(event), 0U) ==
          sizeof(event)) {
        received = true;
        if(level_duration_is_reset(event)) {
            tumospectrum_protocol_runtime_reset_capture(runtime);
            continue;
        }
        const uint32_t duration = level_duration_get_duration(event);
        if(duration < TUMOSPECTRUM_PROTOCOL_MIN_DURATION_US ||
           duration > TUMOSPECTRUM_PROTOCOL_MAX_DURATION_US) {
            tumospectrum_protocol_runtime_reset_capture(runtime);
            continue;
        }
        runtime->pulses[runtime->pulse_count++] =
            level_duration_get_level(event) ? (int32_t)duration : -(int32_t)duration;
        if(duration >= TUMOSPECTRUM_PROTOCOL_FRAME_GAP_US ||
           runtime->pulse_count == ProtocolProfileMaximumCapturePulses) {
            tumospectrum_protocol_runtime_decode(runtime);
        }
    }
    if(!received && runtime->pulse_count > 0U) {
        tumospectrum_protocol_runtime_decode(runtime);
    }
    runtime->rssi = subghz_devices_get_rssi(runtime->device);
}

bool tumospectrum_protocol_runtime_is_listening(const TumoSpectrumProtocolRuntime* runtime) {
    return runtime != NULL && runtime->listening;
}

TumoSpectrumProtocolRadioStatus
    tumospectrum_protocol_runtime_status(const TumoSpectrumProtocolRuntime* runtime) {
    return runtime != NULL ? runtime->radio_status : TumoSpectrumProtocolRadioError;
}

ProtocolProfileStatus
    tumospectrum_protocol_runtime_decode_status(const TumoSpectrumProtocolRuntime* runtime) {
    return runtime != NULL ? runtime->decode_status : ProtocolProfileStatusInvalidArgument;
}

uint16_t tumospectrum_protocol_runtime_pulse_count(const TumoSpectrumProtocolRuntime* runtime) {
    return runtime != NULL ? (uint16_t)runtime->pulse_count : 0U;
}

float tumospectrum_protocol_runtime_rssi(const TumoSpectrumProtocolRuntime* runtime) {
    return runtime != NULL ? runtime->rssi : -127.0f;
}

const char* tumospectrum_protocol_radio_status_name(TumoSpectrumProtocolRadioStatus status) {
    switch(status) {
    case TumoSpectrumProtocolRadioIdle:
        return "Ready";
    case TumoSpectrumProtocolRadioListening:
        return "Listening";
    case TumoSpectrumProtocolRadioBusy:
        return "Radio busy";
    case TumoSpectrumProtocolRadioInvalidFrequency:
        return "Invalid frequency";
    case TumoSpectrumProtocolRadioError:
    default:
        return "Radio error";
    }
}
