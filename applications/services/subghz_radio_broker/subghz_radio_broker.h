#pragma once

#include <furi.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TUMOFLIP_ROADMAP_FULL
#define TUMOFLIP_ROADMAP_FULL 0
#endif

#define TUMOFLIP_RADIO_BROKER_EXTENDED_METADATA TUMOFLIP_ROADMAP_FULL

#define RECORD_SUBGHZ_RADIO_BROKER    "subghz_radio_broker"
#define SUBGHZ_RADIO_BROKER_OWNER_MAX 31U

typedef struct SubGhzRadioBroker SubGhzRadioBroker;

typedef enum {
    SubGhzRadioBrokerDeviceInternal,
    SubGhzRadioBrokerDeviceExternalCC1101,
    SubGhzRadioBrokerDeviceDual,
} SubGhzRadioBrokerDevice;

typedef enum {
    SubGhzRadioBrokerStateIdle,
    SubGhzRadioBrokerStateAcquired,
    SubGhzRadioBrokerStateProbing,
    SubGhzRadioBrokerStateInitialized,
    SubGhzRadioBrokerStateRx,
    SubGhzRadioBrokerStateTx,
    SubGhzRadioBrokerStateAsyncRx,
    SubGhzRadioBrokerStateAsyncTx,
    SubGhzRadioBrokerStateCleaningUp,
    SubGhzRadioBrokerStateExternalPowerOn,
    SubGhzRadioBrokerStateReleasing,
    SubGhzRadioBrokerStateError,
} SubGhzRadioBrokerState;

typedef struct {
    uint32_t token;
} SubGhzRadioBrokerLease;

typedef struct {
    bool busy;
    bool external_powered;
    SubGhzRadioBrokerDevice selected_device;
    char owner[SUBGHZ_RADIO_BROKER_OWNER_MAX + 1];
} SubGhzRadioBrokerStatus;

typedef struct {
    SubGhzRadioBrokerStatus base;
    SubGhzRadioBrokerState state;
} SubGhzRadioBrokerStatusV2;

/** Versioned capability descriptor shared by Standard Sub-GHz and ARF. */
typedef struct {
    uint8_t schema_version;
    SubGhzRadioBrokerDevice device;
    bool can_receive;
    bool can_transmit;
    bool requires_external_power;
    uint32_t rx_min_frequency;
    uint32_t rx_max_frequency;
    uint32_t tx_min_frequency;
    uint32_t tx_max_frequency;
} SubGhzRadioBrokerCapability;

typedef enum {
    SubGhzRadioBrokerPolicyOk,
    SubGhzRadioBrokerPolicyLeaseInvalid,
    SubGhzRadioBrokerPolicyUnsupportedDevice,
    SubGhzRadioBrokerPolicyInvalidFrequency,
    SubGhzRadioBrokerPolicyTxNotAllowed,
    SubGhzRadioBrokerPolicyConfirmationRequired,
    SubGhzRadioBrokerPolicyExternalPowerUnavailable,
} SubGhzRadioBrokerPolicyResult;

/**
 * Bounded metadata for one receive/transmit operation. Payloads and decoded
 * samples are deliberately excluded so this is safe to expose diagnostically.
 */
#if TUMOFLIP_RADIO_BROKER_EXTENDED_METADATA
#define SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH    8U
#else
/* The release profile retains the active session, while the optional history
 * export is reserved for engineering builds. */
#define SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH    1U
#endif
#define SUBGHZ_RADIO_BROKER_SESSION_SOURCE_MAX      15U
#define SUBGHZ_RADIO_BROKER_SESSION_PROTOCOL_MAX    23U
#define SUBGHZ_RADIO_BROKER_SESSION_MODULATION_MAX  15U
#define SUBGHZ_RADIO_BROKER_SESSION_POLICY_MAX      23U
#define SUBGHZ_RADIO_BROKER_PROTOCOL_NAME_MAX       23U
#define SUBGHZ_RADIO_BROKER_PROTOCOL_FIELD_MAX     15U

typedef enum {
    SubGhzRadioBrokerSessionStateActive,
    SubGhzRadioBrokerSessionStatePaused,
    SubGhzRadioBrokerSessionStateClosed,
} SubGhzRadioBrokerSessionState;

/** Bounded capability descriptor for one named ARF/Standard protocol. */
typedef struct {
    uint8_t schema_version;
    bool supported;
    bool can_receive;
    bool can_transmit;
    SubGhzRadioBrokerDevice device;
    uint8_t device_mask;
    uint32_t rx_min_frequency;
    uint32_t rx_max_frequency;
    uint32_t tx_min_frequency;
    uint32_t tx_max_frequency;
    char protocol[SUBGHZ_RADIO_BROKER_PROTOCOL_NAME_MAX + 1U];
    char modulation[SUBGHZ_RADIO_BROKER_PROTOCOL_FIELD_MAX + 1U];
    char bandwidth[SUBGHZ_RADIO_BROKER_PROTOCOL_FIELD_MAX + 1U];
    char preset[SUBGHZ_RADIO_BROKER_PROTOCOL_FIELD_MAX + 1U];
    char reason[SUBGHZ_RADIO_BROKER_PROTOCOL_FIELD_MAX + 1U];
} SubGhzRadioBrokerProtocolCapability;

typedef struct {
    bool active;
    SubGhzRadioBrokerSessionState state;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t started_at;
    uint32_t closed_at;
    uint32_t frequency;
    int16_t rssi_dbm;
    SubGhzRadioBrokerDevice device;
    char source[SUBGHZ_RADIO_BROKER_SESSION_SOURCE_MAX + 1U];
    char protocol[SUBGHZ_RADIO_BROKER_SESSION_PROTOCOL_MAX + 1U];
    char modulation[SUBGHZ_RADIO_BROKER_SESSION_MODULATION_MAX + 1U];
    char policy[SUBGHZ_RADIO_BROKER_SESSION_POLICY_MAX + 1U];
} SubGhzRadioBrokerSessionRecord;

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

bool subghz_radio_broker_set_state(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerState state);

void subghz_radio_broker_get_status(SubGhzRadioBroker* broker, SubGhzRadioBrokerStatus* status);

void subghz_radio_broker_get_status_v2(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerStatusV2* status);

bool subghz_radio_broker_get_capability(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerDevice device,
    SubGhzRadioBrokerCapability* capability);

/** Resolve a protocol-specific capability without allocating or loading a FAP. */
bool subghz_radio_broker_get_protocol_capability(
    SubGhzRadioBroker* broker,
    const char* protocol,
    SubGhzRadioBrokerDevice device,
    SubGhzRadioBrokerProtocolCapability* capability);

SubGhzRadioBrokerPolicyResult subghz_radio_broker_validate_tx(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device,
    uint32_t frequency,
    bool user_confirmed);

const char* subghz_radio_broker_policy_result_to_string(
    SubGhzRadioBrokerPolicyResult result);

bool subghz_radio_broker_session_open(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device,
    const char* source);

bool subghz_radio_broker_session_observe(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    uint32_t frequency,
    const char* protocol,
    const char* modulation,
    int16_t rssi_dbm);

bool subghz_radio_broker_session_pause(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_session_resume(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_session_close(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

size_t subghz_radio_broker_session_history(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerSessionRecord* records,
    size_t record_capacity);

#ifdef __cplusplus
}
#endif
