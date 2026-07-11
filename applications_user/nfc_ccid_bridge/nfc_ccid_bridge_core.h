#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NFC_CCID_BRIDGE_APDU_MAX 255U

typedef enum {
    NfcCcidBridgePolicyReadOnly,
    NfcCcidBridgePolicyFull,
} NfcCcidBridgePolicy;

typedef enum {
    NfcCcidBridgeApduAllowed,
    NfcCcidBridgeApduMalformed,
    NfcCcidBridgeApduBlocked,
} NfcCcidBridgeApduDecision;

NfcCcidBridgeApduDecision
    nfc_ccid_bridge_check_apdu(const uint8_t* apdu, size_t apdu_size, NfcCcidBridgePolicy policy);

uint16_t nfc_ccid_bridge_response_status(const uint8_t* response, size_t response_size);

const char* nfc_ccid_bridge_policy_name(NfcCcidBridgePolicy policy);
