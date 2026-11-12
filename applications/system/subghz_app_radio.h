#ifndef TUMO_SUBGHZ_APP_RADIO_H
#define TUMO_SUBGHZ_APP_RADIO_H

#include <applications/services/subghz_radio_broker/subghz_radio_broker.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <string.h>

/* FAP-local state: no persistent core allocation or competing OTG ownership.
 * The caller must stop its worker/DMA before release. Initialize with {0}. */
typedef struct {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* device;
    bool initialized;
    bool begun;
    bool external;
} SubGhzAppRadio;

static inline bool subghz_app_radio_is_connected(const SubGhzAppRadio* radio) {
    return radio->device &&
           (!radio->external || subghz_devices_is_connect(radio->device));
}

static inline void subghz_app_radio_release(SubGhzAppRadio* radio) {
    if(radio->begun) {
        if(subghz_app_radio_is_connected(radio)) {
            subghz_devices_idle(radio->device);
            subghz_devices_sleep(radio->device);
        }
        subghz_devices_end(radio->device);
    }
    if(radio->initialized) subghz_devices_deinit();
    if(radio->lease.token) subghz_radio_broker_release(radio->broker, &radio->lease);
    if(radio->broker) furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
    memset(radio, 0, sizeof(*radio));
}

static inline bool subghz_app_radio_acquire_policy(
    SubGhzAppRadio* radio,
    const char* owner,
    uint32_t frequency,
    bool allow_external) {
    if(radio->broker) return false;
    radio->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!radio->broker || !subghz_radio_broker_acquire(radio->broker, owner, 0, &radio->lease)) {
        subghz_app_radio_release(radio);
        return false;
    }

    subghz_devices_init();
    radio->initialized = true;
    if(allow_external && subghz_radio_broker_external_power_on(radio->broker, &radio->lease)) {
        const SubGhzDevice* external = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(external && subghz_devices_is_connect(external)) {
            radio->device = external;
            radio->external = true;
        }
    }
    if(!radio->device) {
        subghz_radio_broker_external_power_off(radio->broker, &radio->lease);
        radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    }
    if(!radio->device || !subghz_devices_begin(radio->device)) {
        subghz_app_radio_release(radio);
        return false;
    }
    radio->begun = true;
    if(!subghz_devices_is_frequency_valid(radio->device, frequency)) {
        subghz_app_radio_release(radio);
        return false;
    }
    if(!subghz_radio_broker_set_selected_device(
           radio->broker,
           &radio->lease,
           radio->external ? SubGhzRadioBrokerDeviceExternalCC1101 :
                             SubGhzRadioBrokerDeviceInternal) ||
       !subghz_radio_broker_set_state(
           radio->broker, &radio->lease, SubGhzRadioBrokerStateInitialized)) {
        subghz_app_radio_release(radio);
        return false;
    }
    return true;
}

static inline bool subghz_app_radio_acquire(
    SubGhzAppRadio* radio,
    const char* owner,
    uint32_t frequency) {
    return subghz_app_radio_acquire_policy(radio, owner, frequency, true);
}

static inline bool subghz_app_radio_acquire_internal(
    SubGhzAppRadio* radio,
    const char* owner,
    uint32_t frequency) {
    return subghz_app_radio_acquire_policy(radio, owner, frequency, false);
}

#endif
