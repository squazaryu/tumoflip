#include "tumonet_radio.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <applications/services/subghz_radio_broker/subghz_radio_broker.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>

#include <stdlib.h>
#include <string.h>

#define TAG "TumoNetRadio"

#define TUMONET_RADIO_BROKER_TIMEOUT_MS  500U
#define TUMONET_RADIO_TX_TIMEOUT_MS      150U
#define TUMONET_RADIO_RX_TIMEOUT_MS      180U
#define TUMONET_RADIO_MAX_PACKET         60U
#define TUMONET_RADIO_EXT_SETTLE_MS      30U
#define TUMONET_RADIO_EXT_PROBE_DELAY_MS 20U
#define TUMONET_RADIO_EXT_PROBE_ATTEMPTS 8U

struct TumoNetRadio {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* internal;
    const SubGhzDevice* external;
    const GpioPin* internal_gpio;
    const GpioPin* external_gpio;
    uint32_t frequency;
    bool lease_acquired;
    bool registry_initialized;
    bool external_powered;
    bool external_begun;
    bool open;
};

/* 9.99 kbit/s GFSK, variable packet length, CRC, TumoNet sync word, -30 dBm. */
static uint8_t tumonet_radio_preset[] = {
    0x02, 0x06, /* IOCFG0: sync/end-of-packet */
    0x03, 0x47, /* FIFOTHR */
    0x08, 0x05, /* PKTCTRL0: variable length + CRC */
    0x0B, 0x06, /* FSCTRL1 */
    0x04, 0x54, /* SYNC1: T */
    0x05, 0x4E, /* SYNC0: N */
    0x09, 0x00, /* ADDR */
    0x06, 0x00, /* PKTLEN */
    0x10, 0xC8, /* MDMCFG4 */
    0x11, 0x93, /* MDMCFG3 */
    0x12, 0x12, /* MDMCFG2: GFSK + 16/16 sync */
    0x15, 0x34, /* DEVIATN */
    0x18, 0x18, /* MCSM0 */
    0x19, 0x16, /* FOCCFG */
    0x1B, 0x43, /* AGCCTRL2 */
    0x1C, 0x40, /* AGCCTRL1 */
    0x1D, 0x91, /* AGCCTRL0 */
    0x20, 0xFB, /* WORCTRL */
    0x00, 0x00, /* register list terminator */
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PATABLE */
};

static bool tumonet_radio_cancelled(const volatile bool* cancel_requested) {
    return cancel_requested != NULL && *cancel_requested;
}

static bool tumonet_radio_wait_gpio(
    const GpioPin* pin,
    bool level,
    uint32_t timeout_ms,
    const volatile bool* cancel_requested) {
    const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(timeout_ms);
    while(furi_hal_gpio_read(pin) != level) {
        if(tumonet_radio_cancelled(cancel_requested)) return false;
        if((int32_t)(furi_get_tick() - deadline) >= 0) return false;
        furi_delay_tick(1U);
    }
    return true;
}

static uint32_t tumonet_radio_choose_frequency(void) {
    static const uint32_t candidates[] = {433920000U, 868350000U, 315000000U};
    for(size_t i = 0; i < COUNT_OF(candidates); i++) {
        if(furi_hal_region_is_frequency_allowed(candidates[i]) &&
           furi_hal_subghz_is_tx_allowed(candidates[i])) {
            return candidates[i];
        }
    }
    return 0U;
}

TumoNetRadio* tumonet_radio_alloc(void) {
    return calloc(1U, sizeof(TumoNetRadio));
}

void tumonet_radio_free(TumoNetRadio* radio) {
    if(radio == NULL) return;
    tumonet_radio_close(radio);
    free(radio);
}

TumoNetRadioResult tumonet_radio_open(TumoNetRadio* radio) {
    if(radio == NULL) return TumoNetRadioResultInit;
    if(radio->open) return TumoNetRadioResultOk;

    radio->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(radio->broker == NULL) return TumoNetRadioResultInit;
    if(!subghz_radio_broker_acquire(
           radio->broker,
           "tumonet_bench",
           furi_ms_to_ticks(TUMONET_RADIO_BROKER_TIMEOUT_MS),
           &radio->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        radio->broker = NULL;
        return TumoNetRadioResultBusy;
    }
    radio->lease_acquired = true;
    subghz_radio_broker_set_selected_device(
        radio->broker, &radio->lease, SubGhzRadioBrokerDeviceDual);
    subghz_radio_broker_set_state(radio->broker, &radio->lease, SubGhzRadioBrokerStateProbing);

    subghz_devices_init();
    radio->registry_initialized = true;
    radio->internal = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    radio->external = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(radio->internal == NULL || radio->external == NULL) {
        FURI_LOG_E(TAG, "CC1101 device registry is incomplete");
        tumonet_radio_close(radio);
        return TumoNetRadioResultExternalDriver;
    }

    if(!subghz_radio_broker_external_power_on(radio->broker, &radio->lease)) {
        FURI_LOG_E(TAG, "External CC1101 power failed");
        tumonet_radio_close(radio);
        return TumoNetRadioResultExternalPower;
    }
    radio->external_powered = true;
    furi_delay_ms(TUMONET_RADIO_EXT_SETTLE_MS);

    bool external_connected = false;
    for(uint8_t attempt = 0U; attempt < TUMONET_RADIO_EXT_PROBE_ATTEMPTS; attempt++) {
        if(subghz_devices_is_connect(radio->external)) {
            external_connected = true;
            break;
        }
        furi_delay_ms(TUMONET_RADIO_EXT_PROBE_DELAY_MS);
    }
    if(!external_connected) {
        FURI_LOG_E(TAG, "External CC1101 probe failed");
        tumonet_radio_close(radio);
        return TumoNetRadioResultNoExternal;
    }

    radio->external_begun = true;
    if(!subghz_devices_begin(radio->external)) {
        FURI_LOG_E(TAG, "External CC1101 begin failed");
        tumonet_radio_close(radio);
        return TumoNetRadioResultExternalBegin;
    }

    radio->frequency = tumonet_radio_choose_frequency();
    if(radio->frequency == 0U) {
        tumonet_radio_close(radio);
        return TumoNetRadioResultRegion;
    }

    subghz_devices_reset(radio->internal);
    subghz_devices_reset(radio->external);
    subghz_devices_idle(radio->internal);
    subghz_devices_idle(radio->external);
    subghz_devices_load_preset(radio->internal, FuriHalSubGhzPresetCustom, tumonet_radio_preset);
    subghz_devices_load_preset(radio->external, FuriHalSubGhzPresetCustom, tumonet_radio_preset);
    radio->internal_gpio = subghz_devices_get_data_gpio(radio->internal);
    radio->external_gpio = subghz_devices_get_data_gpio(radio->external);
    if(radio->internal_gpio == NULL || radio->external_gpio == NULL) {
        FURI_LOG_E(TAG, "CC1101 packet GPIO is unavailable");
        tumonet_radio_close(radio);
        return TumoNetRadioResultInit;
    }
    furi_hal_gpio_init(radio->internal_gpio, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(radio->external_gpio, GpioModeInput, GpioPullNo, GpioSpeedLow);
    subghz_devices_set_frequency(radio->internal, radio->frequency);
    subghz_devices_set_frequency(radio->external, radio->frequency);
    subghz_devices_flush_rx(radio->internal);
    subghz_devices_flush_rx(radio->external);
    subghz_devices_flush_tx(radio->internal);
    subghz_devices_flush_tx(radio->external);

    subghz_radio_broker_set_state(radio->broker, &radio->lease, SubGhzRadioBrokerStateInitialized);
    radio->open = true;
    return TumoNetRadioResultOk;
}

void tumonet_radio_close(TumoNetRadio* radio) {
    if(radio == NULL) return;
    if(radio->lease_acquired && radio->broker != NULL) {
        subghz_radio_broker_set_state(
            radio->broker, &radio->lease, SubGhzRadioBrokerStateCleaningUp);
    }

    if(radio->registry_initialized) {
        if(radio->internal != NULL) {
            subghz_devices_idle(radio->internal);
            subghz_devices_flush_rx(radio->internal);
            subghz_devices_flush_tx(radio->internal);
            subghz_devices_sleep(radio->internal);
        }
        if(radio->external != NULL && radio->external_begun) {
            subghz_devices_idle(radio->external);
            subghz_devices_flush_rx(radio->external);
            subghz_devices_flush_tx(radio->external);
            subghz_devices_sleep(radio->external);
            subghz_devices_end(radio->external);
        }
        radio->external_begun = false;
        radio->internal = NULL;
        radio->external = NULL;
        radio->internal_gpio = NULL;
        radio->external_gpio = NULL;
        subghz_devices_deinit();
        radio->registry_initialized = false;
    }
    if(radio->external_powered && radio->broker != NULL && radio->lease_acquired) {
        subghz_radio_broker_external_power_off(radio->broker, &radio->lease);
        radio->external_powered = false;
    }
    if(radio->lease_acquired && radio->broker != NULL) {
        subghz_radio_broker_release(radio->broker, &radio->lease);
        memset(&radio->lease, 0, sizeof(radio->lease));
        radio->lease_acquired = false;
    }
    if(radio->broker != NULL) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        radio->broker = NULL;
    }
    radio->frequency = 0U;
    radio->open = false;
}

TumoNetRadioResult tumonet_radio_transfer(
    TumoNetRadio* radio,
    TumoNetRadioDirection direction,
    const uint8_t* frame,
    uint8_t frame_size,
    uint8_t* received,
    uint8_t* received_size,
    const volatile bool* cancel_requested) {
    if(radio == NULL || !radio->open || frame == NULL || received == NULL ||
       received_size == NULL || frame_size == 0U || frame_size > TUMONET_RADIO_MAX_PACKET) {
        return TumoNetRadioResultInit;
    }
    if(tumonet_radio_cancelled(cancel_requested)) return TumoNetRadioResultCancelled;

    const SubGhzDevice* sender =
        direction == TumoNetRadioDirectionInternalToExternal ? radio->internal : radio->external;
    const SubGhzDevice* receiver =
        direction == TumoNetRadioDirectionInternalToExternal ? radio->external : radio->internal;
    const GpioPin* sender_gpio = direction == TumoNetRadioDirectionInternalToExternal ?
                                     radio->internal_gpio :
                                     radio->external_gpio;
    const GpioPin* receiver_gpio = direction == TumoNetRadioDirectionInternalToExternal ?
                                       radio->external_gpio :
                                       radio->internal_gpio;
    if(sender_gpio == NULL || receiver_gpio == NULL) return TumoNetRadioResultInit;

    subghz_radio_broker_set_state(radio->broker, &radio->lease, SubGhzRadioBrokerStateRx);
    subghz_devices_idle(sender);
    subghz_devices_idle(receiver);
    subghz_devices_flush_rx(receiver);
    subghz_devices_set_rx(receiver);
    furi_delay_ms(4U);

    subghz_devices_flush_tx(sender);
    subghz_devices_write_packet(sender, frame, frame_size);
    subghz_radio_broker_set_state(radio->broker, &radio->lease, SubGhzRadioBrokerStateTx);
    if(!subghz_devices_set_tx(sender)) {
        subghz_devices_idle(receiver);
        return TumoNetRadioResultTx;
    }

    const bool started =
        tumonet_radio_wait_gpio(sender_gpio, true, TUMONET_RADIO_TX_TIMEOUT_MS, cancel_requested);
    if(!started) {
        subghz_devices_idle(sender);
        subghz_devices_idle(receiver);
        subghz_devices_flush_rx(receiver);
        return tumonet_radio_cancelled(cancel_requested) ? TumoNetRadioResultCancelled :
                                                           TumoNetRadioResultTxStartTimeout;
    }
    const bool finished =
        tumonet_radio_wait_gpio(sender_gpio, false, TUMONET_RADIO_TX_TIMEOUT_MS, cancel_requested);
    subghz_devices_idle(sender);
    if(!finished) {
        subghz_devices_idle(receiver);
        subghz_devices_flush_rx(receiver);
        return tumonet_radio_cancelled(cancel_requested) ? TumoNetRadioResultCancelled :
                                                           TumoNetRadioResultTxEndTimeout;
    }

    const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(TUMONET_RADIO_RX_TIMEOUT_MS);
    while(!subghz_devices_rx_pipe_not_empty(receiver)) {
        if(tumonet_radio_cancelled(cancel_requested)) {
            subghz_devices_idle(receiver);
            subghz_devices_flush_rx(receiver);
            return TumoNetRadioResultCancelled;
        }
        if((int32_t)(furi_get_tick() - deadline) >= 0) {
            subghz_devices_idle(receiver);
            subghz_devices_flush_rx(receiver);
            return TumoNetRadioResultRxTimeout;
        }
        furi_delay_tick(1U);
    }

    if(furi_hal_gpio_read(receiver_gpio) &&
       !tumonet_radio_wait_gpio(
           receiver_gpio, false, TUMONET_RADIO_RX_TIMEOUT_MS, cancel_requested)) {
        subghz_devices_idle(receiver);
        subghz_devices_flush_rx(receiver);
        return tumonet_radio_cancelled(cancel_requested) ? TumoNetRadioResultCancelled :
                                                           TumoNetRadioResultRxTimeout;
    }

    if(!subghz_devices_is_rx_data_crc_valid(receiver)) {
        subghz_devices_idle(receiver);
        subghz_devices_flush_rx(receiver);
        return TumoNetRadioResultCrc;
    }

    uint8_t size = 0U;
    subghz_devices_read_packet(receiver, received, &size);
    subghz_devices_idle(receiver);
    subghz_devices_flush_rx(receiver);
    if(size == 0U || size > TUMONET_RADIO_MAX_PACKET) return TumoNetRadioResultCrc;
    *received_size = size;
    subghz_radio_broker_set_state(radio->broker, &radio->lease, SubGhzRadioBrokerStateInitialized);
    return TumoNetRadioResultOk;
}

uint32_t tumonet_radio_frequency(const TumoNetRadio* radio) {
    return radio != NULL ? radio->frequency : 0U;
}

bool tumonet_radio_result_is_retryable(TumoNetRadioResult result) {
    return result == TumoNetRadioResultTxStartTimeout ||
           result == TumoNetRadioResultTxEndTimeout || result == TumoNetRadioResultRxTimeout ||
           result == TumoNetRadioResultCrc;
}

const char* tumonet_radio_result_name(TumoNetRadioResult result) {
    switch(result) {
    case TumoNetRadioResultOk:
        return "OK";
    case TumoNetRadioResultBusy:
        return "RADIO BUSY";
    case TumoNetRadioResultExternalPower:
        return "EXT POWER";
    case TumoNetRadioResultExternalDriver:
        return "EXT DRIVER";
    case TumoNetRadioResultNoExternal:
        return "EXT PROBE";
    case TumoNetRadioResultExternalBegin:
        return "EXT BEGIN";
    case TumoNetRadioResultRegion:
        return "NO TX BAND";
    case TumoNetRadioResultInit:
        return "INIT ERROR";
    case TumoNetRadioResultTx:
        return "TX DENIED";
    case TumoNetRadioResultTxStartTimeout:
        return "TX START";
    case TumoNetRadioResultTxEndTimeout:
        return "TX END";
    case TumoNetRadioResultRxTimeout:
        return "RX WAIT";
    case TumoNetRadioResultCrc:
        return "RF CRC";
    case TumoNetRadioResultCancelled:
        return "STOPPED";
    default:
        return "RADIO ERROR";
    }
}
