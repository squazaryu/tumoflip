#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SUBGHZ_RADIO_BROKER    "subghz_radio_broker"
#define SUBGHZ_RADIO_BROKER_OWNER_MAX 31U

typedef struct SubGhzRadioBroker SubGhzRadioBroker;

typedef enum {
    SubGhzRadioBrokerDeviceInternal,
    SubGhzRadioBrokerDeviceExternalCC1101,
    SubGhzRadioBrokerDeviceDual,
} SubGhzRadioBrokerDevice;

typedef struct {
    uint32_t token;
} SubGhzRadioBrokerLease;

typedef struct {
    bool busy;
    bool external_powered;
    SubGhzRadioBrokerDevice selected_device;
    char owner[SUBGHZ_RADIO_BROKER_OWNER_MAX + 1];
} SubGhzRadioBrokerStatus;

bool subghz_radio_broker_acquire(
    SubGhzRadioBroker* broker,
    const char* owner,
    uint32_t timeout,
    SubGhzRadioBrokerLease* lease);

void subghz_radio_broker_release(SubGhzRadioBroker* broker, SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_external_power_on(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

void subghz_radio_broker_external_power_off(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_set_selected_device(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device);

void subghz_radio_broker_get_status(SubGhzRadioBroker* broker, SubGhzRadioBrokerStatus* status);

#ifdef __cplusplus
}
#endif
