#include "subghz_radio_broker_i.h"

#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <string.h>

#define TAG                                      "SubGhzRadioBroker"
#define SUBGHZ_RADIO_BROKER_POWER_ATTEMPTS       5U
#define SUBGHZ_RADIO_BROKER_POWER_DELAY_MS       10U
#define SUBGHZ_RADIO_BROKER_EXTERNAL_VOLTAGE_MIN 4.5f
#define SUBGHZ_RADIO_BROKER_RX_BAND_1_MIN        281000000U
#define SUBGHZ_RADIO_BROKER_RX_BAND_1_MAX        361000000U
#define SUBGHZ_RADIO_BROKER_RX_BAND_2_MIN        378000000U
#define SUBGHZ_RADIO_BROKER_RX_BAND_2_MAX        481000000U
#define SUBGHZ_RADIO_BROKER_RX_BAND_3_MIN        749000000U
#define SUBGHZ_RADIO_BROKER_RX_BAND_3_MAX        962000000U
#define SUBGHZ_RADIO_BROKER_TX_BAND_1_MIN        299999755U
#define SUBGHZ_RADIO_BROKER_TX_BAND_1_MAX        350000335U
#define SUBGHZ_RADIO_BROKER_TX_BAND_2_MIN        386999938U
#define SUBGHZ_RADIO_BROKER_TX_BAND_2_MAX        467750000U
#define SUBGHZ_RADIO_BROKER_TX_BAND_3_MIN        778999847U
#define SUBGHZ_RADIO_BROKER_TX_BAND_3_MAX        928000000U
#define SUBGHZ_RADIO_BROKER_DEVICE_MASK_INTERNAL (1U << SubGhzRadioBrokerDeviceInternal)
#define SUBGHZ_RADIO_BROKER_DEVICE_MASK_EXTERNAL (1U << SubGhzRadioBrokerDeviceExternalCC1101)
#define SUBGHZ_RADIO_BROKER_DEVICE_MASK_DUAL     (1U << SubGhzRadioBrokerDeviceDual)

struct SubGhzRadioBroker {
    FuriMutex* lease_mutex;
    FuriMutex* state_mutex;
    uint32_t next_token;
    uint32_t active_token;
    bool owns_external_power;
    SubGhzRadioBrokerStatusInternal status;
    SubGhzRadioBrokerSessionRecord session_history[SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH];
    uint8_t session_history_head;
    uint8_t session_history_count;
    uint32_t next_session_id;
    uint8_t active_session_index;
    bool has_active_session;
};

static bool subghz_radio_broker_is_lease_valid(
    const SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    return lease && lease->token && (lease->token == broker->active_token);
}

static void
    subghz_radio_broker_set_state_locked(SubGhzRadioBroker* broker, SubGhzRadioBrokerState state) {
    broker->status.state = state;
    broker->status.sequence++;
    if(broker->status.sequence == 0U) broker->status.sequence = 1U;
}

static void subghz_radio_broker_note_error_locked(SubGhzRadioBroker* broker) {
    broker->status.state = SubGhzRadioBrokerStateError;
}

static void subghz_radio_broker_session_close_locked(SubGhzRadioBroker* broker) {
    if(!broker->has_active_session) return;

    SubGhzRadioBrokerSessionRecord* record =
        &broker->session_history[broker->active_session_index];
    record->active = false;
    record->state = SubGhzRadioBrokerSessionStateClosed;
    record->closed_at = furi_hal_rtc_get_timestamp();
    broker->has_active_session = false;
}

static bool subghz_radio_broker_external_power_available(void) {
    return furi_hal_power_is_otg_enabled() ||
           furi_hal_power_get_usb_voltage() >= SUBGHZ_RADIO_BROKER_EXTERNAL_VOLTAGE_MIN;
}

static void subghz_radio_broker_power_off_locked(SubGhzRadioBroker* broker) {
    if(broker->owns_external_power && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    broker->owns_external_power = false;
    broker->status.base.external_powered = subghz_radio_broker_external_power_available();
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
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    broker->next_token++;
    if(broker->next_token == 0) broker->next_token++;
    broker->active_token = broker->next_token;
    broker->status.session_id = broker->active_token;
    broker->status.sequence = 0U;
    broker->status.base.busy = true;
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    strlcpy(broker->status.base.owner, owner, sizeof(broker->status.base.owner));
    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateAcquired);
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
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        FURI_LOG_W(TAG, "Ignoring invalid radio lease release");
        return;
    }

    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateReleasing);
    subghz_radio_broker_session_close_locked(broker);
    subghz_radio_broker_power_off_locked(broker);
    broker->active_token = 0;
    broker->status.base.busy = false;
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.base.owner[0] = '\0';
    subghz_radio_broker_set_state_locked(broker, SubGhzRadioBrokerStateIdle);
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
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    const bool was_otg_enabled = furi_hal_power_is_otg_enabled();
    for(uint8_t attempt = 0;
        !furi_hal_power_is_otg_enabled() && attempt < SUBGHZ_RADIO_BROKER_POWER_ATTEMPTS;
        attempt++) {
        furi_hal_power_enable_otg();
        furi_delay_ms(SUBGHZ_RADIO_BROKER_POWER_DELAY_MS);
    }
    const bool powered = subghz_radio_broker_external_power_available();
    broker->owns_external_power |= furi_hal_power_is_otg_enabled() && !was_otg_enabled;
    broker->status.base.external_powered = powered;
    subghz_radio_broker_set_state_locked(
        broker, powered ? SubGhzRadioBrokerStateExternalPowerOn : SubGhzRadioBrokerStateError);
    furi_mutex_release(broker->state_mutex);
    if(!powered) FURI_LOG_E(TAG, "External radio power is unavailable");
    return powered;
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
                                       SubGhzRadioBrokerStateIdle);
    } else {
        subghz_radio_broker_note_error_locked(broker);
    }
    furi_mutex_release(broker->state_mutex);
}

bool subghz_radio_broker_set_selected_device(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool valid = subghz_radio_broker_is_lease_valid(broker, lease) &&
                       device <= SubGhzRadioBrokerDeviceDual;
    if(valid) {
        broker->status.base.selected_device = device;
        broker->status.sequence++;
        if(broker->status.sequence == 0U) broker->status.sequence = 1U;
        broker->status.state = broker->status.base.external_powered ?
                                   SubGhzRadioBrokerStateExternalPowerOn :
                                   SubGhzRadioBrokerStateAcquired;
    } else {
        subghz_radio_broker_note_error_locked(broker);
    }
    furi_mutex_release(broker->state_mutex);
    return valid;
}

bool subghz_radio_broker_set_state(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerState state) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool valid = subghz_radio_broker_is_lease_valid(broker, lease) &&
                       state <= SubGhzRadioBrokerStateError;
    if(valid) {
        subghz_radio_broker_set_state_locked(broker, state);
    } else {
        subghz_radio_broker_note_error_locked(broker);
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
    status->base = broker->status.base;
    status->state = broker->status.state;
    furi_mutex_release(broker->state_mutex);
}

void subghz_radio_broker_get_status_internal(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerStatusInternal* status) {
    furi_check(broker);
    furi_check(status);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    *status = broker->status;
    furi_mutex_release(broker->state_mutex);
}

static bool subghz_radio_broker_frequency_is_rx_valid(uint32_t frequency) {
    return (frequency >= SUBGHZ_RADIO_BROKER_RX_BAND_1_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_RX_BAND_1_MAX) ||
           (frequency >= SUBGHZ_RADIO_BROKER_RX_BAND_2_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_RX_BAND_2_MAX) ||
           (frequency >= SUBGHZ_RADIO_BROKER_RX_BAND_3_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_RX_BAND_3_MAX);
}

static bool subghz_radio_broker_frequency_is_tx_allowed(uint32_t frequency) {
    return (frequency >= SUBGHZ_RADIO_BROKER_TX_BAND_1_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_TX_BAND_1_MAX) ||
           (frequency >= SUBGHZ_RADIO_BROKER_TX_BAND_2_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_TX_BAND_2_MAX) ||
           (frequency >= SUBGHZ_RADIO_BROKER_TX_BAND_3_MIN &&
            frequency <= SUBGHZ_RADIO_BROKER_TX_BAND_3_MAX);
}

static void subghz_radio_broker_note_policy(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerPolicyResult result) {
    if(!broker) return;
    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(subghz_radio_broker_is_lease_valid(broker, lease) && broker->has_active_session) {
        SubGhzRadioBrokerSessionRecord* record =
            &broker->session_history[broker->active_session_index];
        strlcpy(
            record->policy,
            subghz_radio_broker_policy_result_to_string(result),
            sizeof(record->policy));
        record->sequence++;
        if(record->sequence == 0U) record->sequence = 1U;
    }
    furi_mutex_release(broker->state_mutex);
}

bool subghz_radio_broker_get_capability(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerDevice device,
    SubGhzRadioBrokerCapability* capability) {
    furi_check(broker);
    furi_check(capability);
    UNUSED(broker);

    if(device > SubGhzRadioBrokerDeviceDual) return false;
    capability->schema_version = 1U;
    capability->device = device;
    capability->can_receive = true;
    capability->can_transmit = true;
    capability->requires_external_power =
        device == SubGhzRadioBrokerDeviceExternalCC1101 ||
        device == SubGhzRadioBrokerDeviceDual;
    capability->rx_min_frequency = SUBGHZ_RADIO_BROKER_RX_BAND_1_MIN;
    capability->rx_max_frequency = SUBGHZ_RADIO_BROKER_RX_BAND_3_MAX;
    capability->tx_min_frequency = SUBGHZ_RADIO_BROKER_TX_BAND_1_MIN;
    capability->tx_max_frequency = SUBGHZ_RADIO_BROKER_TX_BAND_3_MAX;
    return true;
}

bool subghz_radio_broker_get_protocol_capability(
    SubGhzRadioBroker* broker,
    const char* protocol,
    SubGhzRadioBrokerDevice device,
    SubGhzRadioBrokerProtocolCapability* capability) {
    furi_check(broker);
    furi_check(protocol);
    furi_check(capability);
    memset(capability, 0, sizeof(*capability));
    capability->schema_version = 1U;
    capability->device = device;
    strlcpy(capability->protocol, protocol, sizeof(capability->protocol));

    SubGhzRadioBrokerCapability device_capability;
    if(device > SubGhzRadioBrokerDeviceDual ||
       !subghz_radio_broker_get_capability(broker, device, &device_capability)) {
        strlcpy(capability->reason, "device", sizeof(capability->reason));
        return false;
    }

    capability->device_mask = device == SubGhzRadioBrokerDeviceDual ?
                                  (uint8_t)(SUBGHZ_RADIO_BROKER_DEVICE_MASK_INTERNAL |
                                             SUBGHZ_RADIO_BROKER_DEVICE_MASK_EXTERNAL) :
                                  (uint8_t)(1U << device);
    capability->rx_min_frequency = device_capability.rx_min_frequency;
    capability->rx_max_frequency = device_capability.rx_max_frequency;
    capability->tx_min_frequency = device_capability.tx_min_frequency;
    capability->tx_max_frequency = device_capability.tx_max_frequency;
    capability->can_receive = device_capability.can_receive;
    capability->can_transmit = device_capability.can_transmit;
    strlcpy(capability->modulation, "auto", sizeof(capability->modulation));
    strlcpy(capability->bandwidth, "auto", sizeof(capability->bandwidth));
    strlcpy(capability->preset, "auto", sizeof(capability->preset));

    /* The registry deliberately starts with protocol families understood by
     * both Standard Sub-GHz and ARF. Unknown names are rejected instead of
     * being presented as safe-to-transmit capabilities. */
    if(strcasecmp(protocol, "RAW") == 0 || strcasecmp(protocol, "BinRAW") == 0) {
        strlcpy(capability->protocol, "RAW", sizeof(capability->protocol));
        strlcpy(capability->modulation, "OOK/FSK", sizeof(capability->modulation));
        strlcpy(capability->bandwidth, "auto", sizeof(capability->bandwidth));
        capability->supported = true;
    } else if(strcasecmp(protocol, "ProtoPirate") == 0 ||
              strcasecmp(protocol, "ARF") == 0) {
        strlcpy(capability->protocol, "ProtoPirate", sizeof(capability->protocol));
        strlcpy(capability->modulation, "protocol", sizeof(capability->modulation));
        capability->supported = true;
    } else if(strcasecmp(protocol, "Keeloq") == 0 ||
              strcasecmp(protocol, "KeeLoq") == 0) {
        strlcpy(capability->protocol, "Keeloq", sizeof(capability->protocol));
        strlcpy(capability->modulation, "OOK", sizeof(capability->modulation));
        capability->supported = true;
    } else {
        strlcpy(capability->reason, "unknown", sizeof(capability->reason));
        return false;
    }
    capability->reason[0] = '\0';
    return true;
}

SubGhzRadioBrokerPolicyResult subghz_radio_broker_validate_tx(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device,
    uint32_t frequency,
    bool user_confirmed) {
    furi_check(broker);

    SubGhzRadioBrokerPolicyResult result = SubGhzRadioBrokerPolicyOk;
    if(device > SubGhzRadioBrokerDeviceDual) {
        result = SubGhzRadioBrokerPolicyUnsupportedDevice;
        goto done;
    }
    if(!user_confirmed) {
        result = SubGhzRadioBrokerPolicyConfirmationRequired;
        goto done;
    }

    SubGhzRadioBrokerCapability capability;
    if(!subghz_radio_broker_get_capability(broker, device, &capability) ||
       !capability.can_transmit) {
        result = SubGhzRadioBrokerPolicyUnsupportedDevice;
        goto done;
    }

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool lease_valid = subghz_radio_broker_is_lease_valid(broker, lease);
    const bool external_powered = broker->status.base.external_powered;
    furi_mutex_release(broker->state_mutex);
    if(!lease_valid) {
        result = SubGhzRadioBrokerPolicyLeaseInvalid;
        goto done;
    }

    if(!subghz_radio_broker_frequency_is_rx_valid(frequency)) {
        result = SubGhzRadioBrokerPolicyInvalidFrequency;
        goto done;
    }
    /* All transmitters use the same regional allow-list. The external
     * module is not a way around the local regulatory policy. */
    if(!subghz_radio_broker_frequency_is_tx_allowed(frequency)) {
        result = SubGhzRadioBrokerPolicyTxNotAllowed;
        goto done;
    }
    if((device == SubGhzRadioBrokerDeviceExternalCC1101 ||
        device == SubGhzRadioBrokerDeviceDual) &&
       !external_powered) {
        result = SubGhzRadioBrokerPolicyExternalPowerUnavailable;
        goto done;
    }
done:
    subghz_radio_broker_note_policy(broker, lease, result);
    return result;
}

const char* subghz_radio_broker_policy_result_to_string(
    SubGhzRadioBrokerPolicyResult result) {
    switch(result) {
    case SubGhzRadioBrokerPolicyOk:
        return "ok";
    case SubGhzRadioBrokerPolicyLeaseInvalid:
        return "lease_invalid";
    case SubGhzRadioBrokerPolicyUnsupportedDevice:
        return "unsupported_device";
    case SubGhzRadioBrokerPolicyInvalidFrequency:
        return "invalid_frequency";
    case SubGhzRadioBrokerPolicyTxNotAllowed:
        return "tx_not_allowed";
    case SubGhzRadioBrokerPolicyConfirmationRequired:
        return "confirmation_required";
    case SubGhzRadioBrokerPolicyExternalPowerUnavailable:
        return "external_power_unavailable";
    default:
        return "unknown";
    }
}

bool subghz_radio_broker_session_open(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device,
    const char* source) {
    furi_check(broker);
    furi_check(source);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease) || device > SubGhzRadioBrokerDeviceDual) {
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    subghz_radio_broker_session_close_locked(broker);
    const uint8_t index = broker->session_history_head;
    broker->session_history_head =
        (uint8_t)((broker->session_history_head + 1U) % SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH);
    if(broker->session_history_count < SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH) {
        broker->session_history_count++;
    }

    SubGhzRadioBrokerSessionRecord* record = &broker->session_history[index];
    memset(record, 0, sizeof(*record));
    broker->next_session_id++;
    if(broker->next_session_id == 0U) broker->next_session_id = 1U;
    record->active = true;
    record->state = SubGhzRadioBrokerSessionStateActive;
    record->session_id = broker->next_session_id;
    record->sequence = 1U;
    record->started_at = furi_hal_rtc_get_timestamp();
    record->device = device;
    strlcpy(record->source, source, sizeof(record->source));
    strlcpy(record->policy, "not_evaluated", sizeof(record->policy));
    broker->active_session_index = index;
    broker->has_active_session = true;
    furi_mutex_release(broker->state_mutex);
    return true;
}

bool subghz_radio_broker_session_observe(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    uint32_t frequency,
    const char* protocol,
    const char* modulation,
    int16_t rssi_dbm) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease) || !broker->has_active_session ||
       broker->session_history[broker->active_session_index].state !=
           SubGhzRadioBrokerSessionStateActive) {
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        return false;
    }

    SubGhzRadioBrokerSessionRecord* record =
        &broker->session_history[broker->active_session_index];
    if(frequency != 0U) record->frequency = frequency;
    if(protocol && protocol[0] != '\0') {
        strlcpy(record->protocol, protocol, sizeof(record->protocol));
    }
    if(modulation && modulation[0] != '\0') {
        strlcpy(record->modulation, modulation, sizeof(record->modulation));
    }
    record->rssi_dbm = rssi_dbm;
    record->sequence++;
    if(record->sequence == 0U) record->sequence = 1U;
    furi_mutex_release(broker->state_mutex);
    return true;
}

bool subghz_radio_broker_session_pause(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(broker);
    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool valid = subghz_radio_broker_is_lease_valid(broker, lease) &&
                       broker->has_active_session;
    bool paused = false;
    if(valid) {
        SubGhzRadioBrokerSessionRecord* record =
            &broker->session_history[broker->active_session_index];
        if(record->state == SubGhzRadioBrokerSessionStateActive) {
            record->state = SubGhzRadioBrokerSessionStatePaused;
            record->sequence++;
            if(record->sequence == 0U) record->sequence = 1U;
            paused = true;
        } else {
            subghz_radio_broker_note_error_locked(broker);
        }
    } else {
        subghz_radio_broker_note_error_locked(broker);
    }
    furi_mutex_release(broker->state_mutex);
    return valid && paused;
}

bool subghz_radio_broker_session_resume(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(broker);
    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool valid = subghz_radio_broker_is_lease_valid(broker, lease) &&
                       broker->has_active_session;
    bool resumed = false;
    if(valid) {
        SubGhzRadioBrokerSessionRecord* record =
            &broker->session_history[broker->active_session_index];
        if(record->state == SubGhzRadioBrokerSessionStatePaused) {
            record->state = SubGhzRadioBrokerSessionStateActive;
            record->sequence++;
            if(record->sequence == 0U) record->sequence = 1U;
            resumed = true;
        } else {
            subghz_radio_broker_note_error_locked(broker);
        }
    } else {
        subghz_radio_broker_note_error_locked(broker);
    }
    furi_mutex_release(broker->state_mutex);
    return resumed;
}

bool subghz_radio_broker_session_close(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(broker);

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(!subghz_radio_broker_is_lease_valid(broker, lease)) {
        subghz_radio_broker_note_error_locked(broker);
        furi_mutex_release(broker->state_mutex);
        return false;
    }
    subghz_radio_broker_session_close_locked(broker);
    furi_mutex_release(broker->state_mutex);
    return true;
}

size_t subghz_radio_broker_session_history(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerSessionRecord* records,
    size_t record_capacity) {
    furi_check(broker);
    furi_check(records || record_capacity == 0U);
    if(record_capacity == 0U) return 0U;

    furi_check(furi_mutex_acquire(broker->state_mutex, FuriWaitForever) == FuriStatusOk);
    const size_t count =
        broker->session_history_count < record_capacity ? broker->session_history_count :
                                                           record_capacity;
    const size_t first =
        (broker->session_history_head + SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH -
         broker->session_history_count) %
        SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH;
    const size_t skip = broker->session_history_count - count;
    for(size_t offset = 0U; offset < count; offset++) {
        const size_t index =
            (first + skip + offset) % SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH;
        records[offset] = broker->session_history[index];
    }
    furi_mutex_release(broker->state_mutex);
    return count;
}

int32_t subghz_radio_broker_srv(void* context) {
    UNUSED(context);

    SubGhzRadioBroker* broker = malloc(sizeof(SubGhzRadioBroker));
    memset(broker, 0, sizeof(SubGhzRadioBroker));
    broker->lease_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    broker->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    broker->status.base.selected_device = SubGhzRadioBrokerDeviceInternal;
    broker->status.state = SubGhzRadioBrokerStateIdle;
    furi_record_create(RECORD_SUBGHZ_RADIO_BROKER, broker);
    FURI_LOG_I(TAG, "Radio broker ready");

    furi_thread_suspend(furi_thread_get_current_id());
    return 0;
}
