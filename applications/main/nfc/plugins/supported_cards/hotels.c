#include "nfc_supported_card_plugin.h"

#include <flipper_application.h>

#include <nfc/protocols/mf_classic/mf_classic.h>

#include <string.h>

static const uint8_t onity_key[MF_CLASSIC_KEY_SIZE] = {0x8A, 0x19, 0xD4, 0x0C, 0xF2, 0xB5};
static const uint8_t vingcard_key[MF_CLASSIC_KEY_SIZE] = {0x00, 0x00, 0x01, 0x4B, 0x5C, 0x31};

static bool hotels_key_matches(
    const MfClassicData* data,
    uint8_t sector,
    MfClassicKeyType key_type,
    const uint8_t expected_key[MF_CLASSIC_KEY_SIZE]) {
    if(sector >= mf_classic_get_total_sectors_num(data->type)) return false;
    if(!mf_classic_is_key_found(data, sector, key_type)) return false;

    const MfClassicKey key = mf_classic_get_key(data, sector, key_type);
    return memcmp(key.data, expected_key, sizeof(key.data)) == 0;
}

static bool hotels_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);
    furi_assert(parsed_data);

    if(nfc_device_get_protocol(device) != NfcProtocolMfClassic) return false;

    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);
    if(!data || data->type >= MfClassicTypeNum) return false;
    if(mf_classic_get_total_sectors_num(data->type) <= 2) return false;

    const bool onity = hotels_key_matches(data, 1, MfClassicKeyTypeA, onity_key);
    const bool vingcard = hotels_key_matches(data, 2, MfClassicKeyTypeB, vingcard_key);
    if(!onity && !vingcard) return false;

    if(onity && vingcard) {
        furi_string_printf(parsed_data, "\e#Hotel card\nPossible systems:\nOnity\nVingCard");
    } else if(onity) {
        furi_string_printf(parsed_data, "\e#Hotel card\nPossible system: Onity");
    } else {
        furi_string_printf(parsed_data, "\e#Hotel card\nPossible system: VingCard");
    }

    return true;
}

static const NfcSupportedCardsPlugin hotels_plugin = {
    .protocol = NfcProtocolMfClassic,
    .verify = NULL,
    .read = NULL,
    .parse = hotels_parse,
};

static const FlipperAppPluginDescriptor hotels_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &hotels_plugin,
};

const FlipperAppPluginDescriptor* hotels_plugin_ep(void) {
    return &hotels_plugin_descriptor;
}
