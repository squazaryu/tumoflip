#include "tumospectrum_envelope.h"

#include <furi.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/devices.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct TumoSpectrumEnvelope {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* device;
    bool devices_initialized;
    bool device_begun;
    TumoEnvelopeSession session;
    TumoEnvelopeSnapshot snapshot;
};

void tumospectrum_envelope_calibrate(TumoSpectrumEnvelope* envelope) {
    if(!envelope) return;
    tumo_envelope_session_reset(&envelope->session);
    envelope->snapshot.kind = TumoEnvelopeLearning;
    envelope->snapshot.agreement = 0;
    envelope->snapshot.calibrated = false;
    envelope->snapshot.floor_dbm = -127;
    envelope->snapshot.high_dbm = -127;
    envelope->snapshot.sample_period_us = 0;
    memset(envelope->snapshot.samples, -127, sizeof(envelope->snapshot.samples));
}

TumoSpectrumEnvelope* tumospectrum_envelope_alloc(void) {
    TumoSpectrumEnvelope* envelope = calloc(1, sizeof(*envelope));
    if(envelope) {
        envelope->snapshot.frequency_hz = 433920000U;
        tumospectrum_envelope_calibrate(envelope);
    }
    return envelope;
}

void tumospectrum_envelope_stop(TumoSpectrumEnvelope* envelope) {
    if(!envelope) return;
    if(envelope->broker) {
        subghz_radio_broker_set_state(
            envelope->broker, &envelope->lease, SubGhzRadioBrokerStateCleaningUp);
    }
    if(envelope->device_begun) {
        subghz_devices_idle(envelope->device);
        subghz_devices_sleep(envelope->device);
        subghz_devices_end(envelope->device);
        envelope->device_begun = false;
    }
    envelope->device = NULL;
    if(envelope->devices_initialized) {
        subghz_devices_deinit();
        envelope->devices_initialized = false;
    }
    if(envelope->broker) {
        subghz_radio_broker_release(envelope->broker, &envelope->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        envelope->broker = NULL;
    }
    envelope->snapshot.status = TumoEnvelopeIdle;
    tumospectrum_envelope_calibrate(envelope);
}

void tumospectrum_envelope_free(TumoSpectrumEnvelope* envelope) {
    if(!envelope) return;
    tumospectrum_envelope_stop(envelope);
    free(envelope);
}

static bool tumospectrum_envelope_fail(TumoSpectrumEnvelope* envelope, TumoEnvelopeStatus status) {
    tumospectrum_envelope_stop(envelope);
    envelope->snapshot.status = status;
    return false;
}

bool tumospectrum_envelope_start(
    TumoSpectrumEnvelope* envelope,
    uint32_t frequency,
    bool external) {
    if(!envelope) return false;
    tumospectrum_envelope_stop(envelope);
    envelope->snapshot.frequency_hz = frequency;
    envelope->snapshot.external = external;
    if(!furi_hal_subghz_is_frequency_valid(frequency)) {
        return tumospectrum_envelope_fail(envelope, TumoEnvelopeError);
    }
    envelope->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!subghz_radio_broker_acquire(
           envelope->broker, "tumospectrum_envelope", 500U, &envelope->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        envelope->broker = NULL;
        envelope->snapshot.status = TumoEnvelopeBusy;
        return false;
    }
    subghz_radio_broker_set_state(
        envelope->broker, &envelope->lease, SubGhzRadioBrokerStateProbing);
    subghz_devices_init();
    envelope->devices_initialized = true;
    if(external &&
       !subghz_radio_broker_external_power_on(envelope->broker, &envelope->lease)) {
        return tumospectrum_envelope_fail(envelope, TumoEnvelopeMissing);
    }
    envelope->device = subghz_devices_get_by_name(external ? "cc1101_ext" : "cc1101_int");
    if(!envelope->device || (external && !subghz_devices_is_connect(envelope->device))) {
        return tumospectrum_envelope_fail(envelope, TumoEnvelopeMissing);
    }
    if(external && !subghz_devices_begin(envelope->device)) {
        // The external driver can allocate a context before returning false.
        subghz_devices_end(envelope->device);
        return tumospectrum_envelope_fail(envelope, TumoEnvelopeMissing);
    }
    envelope->device_begun = true;
    subghz_radio_broker_set_selected_device(
        envelope->broker,
        &envelope->lease,
        external ? SubGhzRadioBrokerDeviceExternalCC1101 : SubGhzRadioBrokerDeviceInternal);
    subghz_devices_reset(envelope->device);
    // Same fixed receive passband for both radios. No register/ABI changes and
    // no automatic retuning or TX based on the envelope result.
    subghz_devices_load_preset(envelope->device, FuriHalSubGhzPreset2FSKDev476Async, NULL);
    if(!subghz_devices_set_frequency(envelope->device, frequency)) {
        return tumospectrum_envelope_fail(envelope, TumoEnvelopeError);
    }
    subghz_radio_broker_set_state(
        envelope->broker, &envelope->lease, SubGhzRadioBrokerStateInitialized);
    subghz_devices_set_rx(envelope->device);
    furi_delay_ms(5);
    subghz_radio_broker_set_state(
        envelope->broker, &envelope->lease, SubGhzRadioBrokerStateRx);
    envelope->snapshot.status = TumoEnvelopeRunning;
    return true;
}

void tumospectrum_envelope_tick(TumoSpectrumEnvelope* envelope) {
    if(!envelope || envelope->snapshot.status != TumoEnvelopeRunning) return;
    if(envelope->snapshot.external && !subghz_devices_is_connect(envelope->device)) {
        tumospectrum_envelope_fail(envelope, TumoEnvelopeMissing);
        return;
    }
    const uint32_t started = furi_get_tick();
    for(unsigned i = 0; i < TUMO_ENVELOPE_SAMPLES; i++) {
        const float value = subghz_devices_get_rssi(envelope->device);
        if(!isfinite(value) || value < -127.0f || value > -1.0f ||
           furi_get_tick() - started > 50U) {
            tumospectrum_envelope_fail(envelope, TumoEnvelopeError);
            return;
        }
        envelope->snapshot.samples[i] = (int8_t)value;
        // Own/release SPI per sample; no IRQ masking, high-priority worker or
        // long exclusive SPI burst. Yield to BLE/SD between groups.
        if(i + 1 < TUMO_ENVELOPE_SAMPLES) furi_delay_us(100);
        if((i + 1) % 16 == 0) furi_thread_yield();
    }
    // Upper estimate from the millisecond tick, not a calibrated sample clock.
    envelope->snapshot.sample_period_us =
        (uint16_t)(((furi_get_tick() - started + 1U) * 1000U) / (TUMO_ENVELOPE_SAMPLES - 1U));
    TumoEnvelopeWindow window;
    if(!tumo_envelope_analyze(
           envelope->snapshot.samples,
           TUMO_ENVELOPE_SAMPLES,
           envelope->snapshot.floor_dbm,
           &window)) {
        tumospectrum_envelope_fail(envelope, TumoEnvelopeError);
        return;
    }
    envelope->snapshot.high_dbm = window.high_dbm;
    if(!envelope->snapshot.calibrated) {
        // Start or OK explicitly learns quiet-channel p10. A transmitter held
        // during calibration may hide a signal; the screen instructs TX off.
        envelope->snapshot.floor_dbm = window.low_dbm;
        envelope->snapshot.calibrated = true;
        return;
    }
    tumo_envelope_session_add(&envelope->session, &window);
    envelope->snapshot.kind = envelope->session.kind;
    envelope->snapshot.agreement = envelope->session.agreement;
}

bool tumospectrum_envelope_tune(TumoSpectrumEnvelope* envelope, int direction) {
    if(!envelope || !direction) return false;
    const int64_t frequency = (int64_t)envelope->snapshot.frequency_hz +
                              (direction > 0 ? 25000 : -25000);
    if(frequency < 0 || frequency > UINT32_MAX ||
       !furi_hal_subghz_is_frequency_valid((uint32_t)frequency)) {
        return false;
    }
    return tumospectrum_envelope_start(
        envelope, (uint32_t)frequency, envelope->snapshot.external);
}

bool tumospectrum_envelope_cycle_radio(TumoSpectrumEnvelope* envelope) {
    return envelope && tumospectrum_envelope_start(
                           envelope, envelope->snapshot.frequency_hz, !envelope->snapshot.external);
}

void tumospectrum_envelope_next_band(TumoSpectrumEnvelope* envelope) {
    if(!envelope) return;
    static const uint32_t frequencies[] = {315000000U, 390000000U, 433920000U, 868350000U, 915000000U};
    uint32_t next = frequencies[0];
    for(unsigned i = 0; i < COUNT_OF(frequencies); i++) {
        if(frequencies[i] > envelope->snapshot.frequency_hz) {
            next = frequencies[i];
            break;
        }
    }
    tumospectrum_envelope_start(envelope, next, envelope->snapshot.external);
}

void tumospectrum_envelope_snapshot(
    const TumoSpectrumEnvelope* envelope,
    TumoEnvelopeSnapshot* snapshot) {
    if(!snapshot) return;
    *snapshot = (TumoEnvelopeSnapshot){0};
    if(envelope) *snapshot = envelope->snapshot;
}
