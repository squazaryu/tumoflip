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
    SubGhzRadioBrokerStatusV2 status;
};

static bool subghz_radio_broker_is_lease_valid(
    const SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    return lease && lease->token && (lease->token == broker->active_token);
}

static void subghz_radio_broker_set_state_locked(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerState state,
    const char* last_error) {
    broker->status.state = state;
    broker->status.last_transition_tick = furi_get_tick();
    if(last_error) {
        strlcpy(broker->status.last_error, last_error, sizeof(broker->status.last_error));
    } else {
        broker->status.last_error[0] = '\0';
    }
}

static void subghz_radio_broker_note_error_locked(
    SubGhzRadioBroker* broker,
    const char* last_error) {
    broker->status.last_transition_tick = furi_get_tick();
    strlcpy(broker->status.last_error, last_error, sizeof(broker->status.last_error));
    if(!broker->status.base.busy) broker->status.state = SubGhzRadioBrokerStateError;
}

static void subghz_radio_broker_power_off_locked(SubGhzRadioBroker* broker) {
    if(broker->owns_external_power && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    broker->owns_external_power = false;
    broker->status.base.external_powered = furi_hal_power_is_otg_enabled();
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
    if(furi_mutex_acquire(broker->lease_mutex, timeout) != FuriStatusOk) {
        furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
        subghz_radio_broker_note_error_locked(broker, "acquire_timeout");
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    broker->next_token++;
    if(broker->next_token == 0) broker->next_token++;
    broker->active_token = broker->next_token;
    broker->status.base.busy = true;
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.acquired_tick = furi_get_tick();
    strlcpy(broker->status.base.owner, owner, sizeof(broker->status.base.owner));
    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateAcquired, NULL);
    lease->token = broker->active_token;
    char owner_copy[sizeof(broker->status.base.owner)];
    strlcpy(owner_copy, broker->status.base.owner, sizeof(owner_copy));
    furi_mutex_release(broker->state_mutex);

    FURI_LOG_I(TAG, "Radio acquired by %s", owner_copy);
    return true;
}

void subghz_radio_broker_release(SubGhzRadioBroker* broker, SubGhzRadioBrokerLease* lease) {
    furi_check(broker);
    furi_check(lease);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease)) {
        subghz_radio_broker_note_error_locked(broker, "invalid_release");
        furi_mutex_release(broker->state_mutex);
        FURI_LOG_W(TAG, "Ignoring invalid radio lease release");
        return;
    }

    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateReleasing, NULL);
    subghz_radio_broker_power_off_locked(broker);
    broker->active_token = 0;
    broker->status.base.busy = false;
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.acquired_tick = 0;
    broker->status.base.owner[0] = '\0';
    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateIdle, NULL);
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
        subghz_radio_broker_note_error_locked(broker, "invalid_lease");
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
    broker->status.base.external_powered = enabled;
    subghz_radio_broker_set_state_locked(
        broker,
        enabled ? SubGhzRadioBrokerStateExternalPowerOn : SubGhzRadioBrokerStateError,
        enabled ? NULL : "external_power_failed");
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
        subghz_radio_broker_set_state_locked(
            broker,
            broker->status.base.busy ? SubGhzRadioBrokerStateAcquired :
                                       SubGhzRadioBrokerStateIdle,
            NULL);
    } else {
        subghz_radio_broker_note_error_locked(broker, "invalid_lease");
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
    if(valid) {
        broker->status.base.selected_device = device;
        broker->status.last_transition_tick = furi_get_tick();
        broker->status.last_error[0] = '\0';
        broker->status.state = broker->status.base.external_powered ?
                                   SubGhzRadioBrokerStateExternalPowerOn :
                                   SubGhzRadioBrokerStateAcquired;
    } else {
        subghz_radio_broker_note_error_locked(broker, "invalid_lease");
    }
    furi_mutex_release(broker->state_mutex);
    return valid;
}

void subghz_radio_broker_get_status(SubGhzRadioBroker* broker, SubGhzRadioBrokerStatus* status) {
    furi_check(broker);
    furi_check(status);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    *status = broker->status.base;
    furi_mutex_release(broker->state_mutex);
}

void subghz_radio_broker_get_status_v2(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerStatusV2* status) {
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
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.state = SubGhzRadioBrokerStateIdle;
    broker->status.last_transition_tick = furi_get_tick();
    furi_record_create(RECORD_SUBGHZ_RADIO_BROKER, broker);
    FURI_LOG_I(TAG, "Radio broker ready");

    furi_thread_suspend(furi_thread_get_current_id());
    return 0;
}
