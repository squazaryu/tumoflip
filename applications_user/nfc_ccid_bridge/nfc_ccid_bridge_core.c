#include "nfc_ccid_bridge_core.h"

static bool nfc_ccid_bridge_is_read_only_ins(uint8_t ins) {
    switch(ins) {
    case 0xA4: // SELECT
    case 0xB0: // READ BINARY
    case 0xB1: // READ BINARY (extended offset)
    case 0xC0: // GET RESPONSE
    case 0xCA: // GET DATA
    case 0xCB: // GET DATA (odd instruction)
        return true;
    default:
        return false;
    }
}

static bool nfc_ccid_bridge_apdu_length_valid(const uint8_t* apdu, size_t apdu_size) {
    if(!apdu || apdu_size < 4U || apdu_size > NFC_CCID_BRIDGE_APDU_MAX) return false;
    if(apdu_size <= 5U) return true;

    const size_t lc = apdu[4];
    if(lc == 0U) return false;
    return apdu_size == 5U + lc || apdu_size == 6U + lc;
}

NfcCcidBridgeApduDecision
    nfc_ccid_bridge_check_apdu(const uint8_t* apdu, size_t apdu_size, NfcCcidBridgePolicy policy) {
    if(!nfc_ccid_bridge_apdu_length_valid(apdu, apdu_size)) return NfcCcidBridgeApduMalformed;
    if(policy == NfcCcidBridgePolicyReadOnly && !nfc_ccid_bridge_is_read_only_ins(apdu[1]))
        return NfcCcidBridgeApduBlocked;
    return NfcCcidBridgeApduAllowed;
}

uint16_t nfc_ccid_bridge_response_status(const uint8_t* response, size_t response_size) {
    if(!response || response_size < 2U) return 0U;
    return ((uint16_t)response[response_size - 2U] << 8U) | response[response_size - 1U];
}

const char* nfc_ccid_bridge_policy_name(NfcCcidBridgePolicy policy) {
    return policy == NfcCcidBridgePolicyFull ? "FULL" : "READ";
}
