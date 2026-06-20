#include "subghz_radio_broker.h"

#include <furi_hal_power.h>
#include <string.h>

#define TAG                                "SubGhzRadioBroker"
#define SUBGHZ_RADIO_BROKER_POWER_ATTEMPTS 5U
#define SUBGHZ_RADIO_BROKER_POWER_DELAY_MS 10U

struct SubGhzRadioBroker {
    FuriMutex* lease_mutex;
    FuriMutex* state_mutex;
    uint32_t next_token;
    uint32_t active_token;
    bool owns_external_power;
    SubGhzRadioBrokerStatus status;
};

static bool subghz_radio_broker_is_lease_valid(
    const SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    return lease && lease->token && (lease->token == broker->active_token);
}

static void subghz_radio_broker_power_off_locked(SubGhzRadioBroker* broker) {
    if(broker->owns_external_power && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    broker->owns_external_power = false;
    broker->status.external_powered = furi_hal_power_is_otg_enabled();
}

bool subghz_radio_broker_acquire(
    SubGhzRadioBroker* broker,
    const char* owner,
    uint32_t timeout,
    SubGhzRadioBrokerLease* lease) {
    furi_check(broker);
    furi_check(owner);
    furi_check(lease);

    lease->token = 0;
    if(furi_mutex_acquire(broker->lease_mutex, timeout) != FuriStatusOk) return false;

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    broker->next_token++;
    if(broker->next_token == 0) broker->next_token++;
    broker->active_token = broker->next_token;
    broker->status.busy = true;
    broker->status.selected_device = SubGhzRadioBrokerDeviceInternal;
    strlcpy(broker->status.owner, owner, sizeof(broker->status.owner));
    lease->token = broker->active_token;
    furi_mutex_release(broker->state_mutex);

    FURI_LOG_I(TAG, "Radio acquired by %s", broker->status.owner);
    return true;
}

void subghz_radio_broker_release(SubGhzRadioBroker* broker, SubGhzRadioBrokerLease* lease) {
    furi_check(broker);
    furi_check(lease);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease)) {
        furi_mutex_release(broker->state_mutex);
        FURI_LOG_W(TAG, "Ignoring invalid radio lease release");
        return;
    }

    subghz_radio_broker_power_off_locked(broker);
    broker->active_token = 0;
    broker->status.busy = false;
    broker->status.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.owner[0] = '\0';
    lease->token = 0;
    furi_mutex_release(broker->state_mutex);
    furi_mutex_release(broker->lease_mutex);
    FURI_LOG_I(TAG, "Radio released");
}

bool subghz_radio_broker_external_power_on(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease)) {
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    const bool was_enabled = furi_hal_power_is_otg_enabled();
    for(uint8_t attempt = 0;
        !furi_hal_power_is_otg_enabled() && attempt < SUBGHZ_RADIO_BROKER_POWER_ATTEMPTS;
        attempt++) {
        furi_hal_power_enable_otg();
        furi_delay_ms(SUBGHZ_RADIO_BROKER_POWER_DELAY_MS);
    }
    const bool enabled = furi_hal_power_is_otg_enabled();
    broker->owns_external_power |= enabled && !was_enabled;
    broker->status.external_powered = enabled;
    furi_mutex_release(broker->state_mutex);
    if(!enabled) FURI_LOG_E(TAG, "Failed to enable external radio power");
    return enabled;
}

void subghz_radio_broker_external_power_off(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(subghz_radio_broker_is_lease_valid(broker, lease)) {
        subghz_radio_broker_power_off_locked(broker);
    }
    furi_mutex_release(broker->state_mutex);
}

bool subghz_radio_broker_set_selected_device(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool valid = subghz_radio_broker_is_lease_valid(broker, lease);
    if(valid) broker->status.selected_device = device;
    furi_mutex_release(broker->state_mutex);
    return valid;
}

void subghz_radio_broker_get_status(SubGhzRadioBroker* broker, SubGhzRadioBrokerStatus* status) {
    furi_check(broker);
    furi_check(status);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    *status = broker->status;
    furi_mutex_release(broker->state_mutex);
}

int32_t subghz_radio_broker_srv(void* context) {
    UNUSED(context);

    SubGhzRadioBroker* broker = malloc(sizeof(SubGhzRadioBroker));
    memset(broker, 0, sizeof(SubGhzRadioBroker));
    broker->lease_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    broker->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    broker->status.selected_device = SubGhzRadioBrokerDeviceInternal;
    furi_record_create(RECORD_SUBGHZ_RADIO_BROKER, broker);
    FURI_LOG_I(TAG, "Radio broker ready");

    furi_thread_suspend(furi_thread_get_current_id());
    return 0;
}
