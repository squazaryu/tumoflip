#include "tumotag_compare.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

#include <stdio.h>
#include <string.h>

static void tumotag_copy_text(char* destination, size_t size, const char* source) {
    if(size == 0U) return;
    snprintf(destination, size, "%s", source ? source : "");
}

static void tumotag_format_hex(
    char* destination,
    size_t destination_size,
    const uint8_t* data,
    size_t data_size) {
    if(destination_size == 0U) return;
    destination[0] = '\0';
    if(!data) return;

    size_t offset = 0U;
    for(size_t index = 0U; index < data_size && offset + 2U < destination_size; index++) {
        const int written =
            snprintf(destination + offset, destination_size - offset, "%02X", data[index]);
        if(written != 2) break;
        offset += 2U;
    }
}

void tumotag_result_reset(TumoTagResult* result, TumoTagMedium medium) {
    furi_check(result);
    memset(result, 0, sizeof(*result));
    result->medium = medium;
    result->verdict = TumoTagVerdictUnsupported;
}

const char* tumotag_medium_name(TumoTagMedium medium) {
    switch(medium) {
    case TumoTagMediumNfc:
        return "NFC";
    case TumoTagMediumLfRfid:
        return "LF RFID";
    case TumoTagMediumIButton:
        return "iButton";
    default:
        return "Unknown";
    }
}

const char* tumotag_verdict_name(TumoTagVerdict verdict) {
    switch(verdict) {
    case TumoTagVerdictVerified:
        return "VERIFIED";
    case TumoTagVerdictPartial:
        return "PARTIAL";
    case TumoTagVerdictDifferent:
        return "DIFFERENT";
    case TumoTagVerdictUnsupported:
        return "UNSUPPORTED";
    default:
        return "UNKNOWN";
    }
}

static bool tumotag_nfc_identity(
    const NfcDevice* expected,
    const NfcDevice* observed,
    TumoTagResult* result) {
    size_t expected_uid_size = 0U;
    size_t observed_uid_size = 0U;
    const uint8_t* expected_uid = nfc_device_get_uid(expected, &expected_uid_size);
    const uint8_t* observed_uid = nfc_device_get_uid(observed, &observed_uid_size);
    tumotag_format_hex(
        result->expected_id, sizeof(result->expected_id), expected_uid, expected_uid_size);
    tumotag_format_hex(
        result->observed_id, sizeof(result->observed_id), observed_uid, observed_uid_size);

    if(expected_uid_size != observed_uid_size ||
       memcmp(expected_uid, observed_uid, expected_uid_size) != 0) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(result->detail, sizeof(result->detail), "UID differs");
        return false;
    }

    const NfcProtocol expected_protocol = nfc_device_get_protocol(expected);
    const NfcProtocol observed_protocol = nfc_device_get_protocol(observed);
    if(expected_protocol != observed_protocol) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(result->detail, sizeof(result->detail), "Protocol differs");
        return false;
    }

    if(expected_protocol == NfcProtocolIso14443_3a ||
       nfc_protocol_has_parent(expected_protocol, NfcProtocolIso14443_3a)) {
        const Iso14443_3aData* expected_base =
            nfc_device_get_data(expected, NfcProtocolIso14443_3a);
        const Iso14443_3aData* observed_base =
            nfc_device_get_data(observed, NfcProtocolIso14443_3a);
        uint8_t expected_atqa[2];
        uint8_t observed_atqa[2];
        iso14443_3a_get_atqa(expected_base, expected_atqa);
        iso14443_3a_get_atqa(observed_base, observed_atqa);
        if(memcmp(expected_atqa, observed_atqa, sizeof(expected_atqa)) != 0) {
            result->verdict = TumoTagVerdictDifferent;
            tumotag_copy_text(result->detail, sizeof(result->detail), "ATQA differs");
            return false;
        }
        if(iso14443_3a_get_sak(expected_base) != iso14443_3a_get_sak(observed_base)) {
            result->verdict = TumoTagVerdictDifferent;
            tumotag_copy_text(result->detail, sizeof(result->detail), "SAK differs");
            return false;
        }
    }

    return true;
}

static void tumotag_compare_mf_ultralight(
    const NfcDevice* expected,
    const NfcDevice* observed,
    TumoTagResult* result) {
    const MfUltralightData* expected_data = nfc_device_get_data(expected, NfcProtocolMfUltralight);
    const MfUltralightData* observed_data = nfc_device_get_data(observed, NfcProtocolMfUltralight);
    const uint16_t common_pages = MIN(expected_data->pages_read, observed_data->pages_read);

    for(uint16_t page = 0U; page < common_pages; page++) {
        result->compared_units++;
        if(memcmp(
               expected_data->page[page].data,
               observed_data->page[page].data,
               MF_ULTRALIGHT_PAGE_SIZE) != 0) {
            result->verdict = TumoTagVerdictDifferent;
            result->mismatched_units = 1U;
            snprintf(result->detail, sizeof(result->detail), "Page %u differs", page);
            return;
        }
    }

    if(observed_data->pages_read < expected_data->pages_read) {
        result->verdict = TumoTagVerdictPartial;
        result->missing_units = expected_data->pages_read - observed_data->pages_read;
        snprintf(
            result->detail,
            sizeof(result->detail),
            "Pages read %u/%u",
            observed_data->pages_read,
            expected_data->pages_read);
        return;
    }

    if(!mf_ultralight_is_all_data_read(expected_data) ||
       !mf_ultralight_is_all_data_read(observed_data)) {
        result->verdict = TumoTagVerdictPartial;
        snprintf(
            result->detail,
            sizeof(result->detail),
            "Readable pages match; protected data not proven");
        return;
    }

    result->verdict = TumoTagVerdictDifferent;
    tumotag_copy_text(result->detail, sizeof(result->detail), "Ultralight metadata differs");
}

static void tumotag_compare_mf_classic(
    const NfcDevice* expected,
    const NfcDevice* observed,
    TumoTagResult* result) {
    const MfClassicData* expected_data = nfc_device_get_data(expected, NfcProtocolMfClassic);
    const MfClassicData* observed_data = nfc_device_get_data(observed, NfcProtocolMfClassic);
    const uint16_t block_count = mf_classic_get_total_block_num(expected_data->type);

    if(expected_data->type != observed_data->type) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(result->detail, sizeof(result->detail), "MIFARE type differs");
        return;
    }

    for(uint16_t block = 0U; block < block_count; block++) {
        const bool expected_read = mf_classic_is_block_read(expected_data, block);
        const bool observed_read = mf_classic_is_block_read(observed_data, block);
        if(expected_read && observed_read) {
            result->compared_units++;
            if(memcmp(
                   expected_data->block[block].data,
                   observed_data->block[block].data,
                   MF_CLASSIC_BLOCK_SIZE) != 0) {
                result->verdict = TumoTagVerdictDifferent;
                result->mismatched_units = 1U;
                snprintf(result->detail, sizeof(result->detail), "Block %u differs", block);
                return;
            }
        } else if(expected_read && !observed_read) {
            result->missing_units++;
        }
    }

    if(result->missing_units > 0U || !mf_classic_is_card_read(expected_data) ||
       !mf_classic_is_card_read(observed_data)) {
        result->verdict = TumoTagVerdictPartial;
        snprintf(
            result->detail,
            sizeof(result->detail),
            "%u blocks match; %u unavailable",
            result->compared_units,
            result->missing_units);
        return;
    }

    result->verdict = TumoTagVerdictDifferent;
    tumotag_copy_text(result->detail, sizeof(result->detail), "MIFARE metadata or keys differ");
}

bool tumotag_compare_nfc_files(
    const char* expected_path,
    const char* observed_path,
    TumoTagResult* result) {
    furi_check(expected_path);
    furi_check(observed_path);
    furi_check(result);

    tumotag_result_reset(result, TumoTagMediumNfc);
    tumotag_copy_text(result->expected_path, sizeof(result->expected_path), expected_path);
    NfcDevice* expected = nfc_device_alloc();
    NfcDevice* observed = nfc_device_alloc();
    bool loaded = nfc_device_load(expected, expected_path) &&
                  nfc_device_load(observed, observed_path);

    if(!loaded) {
        tumotag_copy_text(
            result->detail, sizeof(result->detail), "Unsupported or damaged NFC file");
        nfc_device_free(observed);
        nfc_device_free(expected);
        return false;
    }

    const NfcProtocol expected_protocol = nfc_device_get_protocol(expected);
    const NfcProtocol observed_protocol = nfc_device_get_protocol(observed);
    tumotag_copy_text(
        result->expected_protocol,
        sizeof(result->expected_protocol),
        nfc_device_get_protocol_name(expected_protocol));
    tumotag_copy_text(
        result->observed_protocol,
        sizeof(result->observed_protocol),
        nfc_device_get_protocol_name(observed_protocol));

    if(!tumotag_nfc_identity(expected, observed, result)) {
        loaded = true;
    } else if(nfc_device_is_equal(expected, observed)) {
        result->verdict = TumoTagVerdictVerified;
        tumotag_copy_text(
            result->detail, sizeof(result->detail), "Identity and readable data match");
    } else if(expected_protocol == NfcProtocolMfUltralight) {
        tumotag_compare_mf_ultralight(expected, observed, result);
    } else if(expected_protocol == NfcProtocolMfClassic) {
        tumotag_compare_mf_classic(expected, observed, result);
    } else if(
        expected_protocol == NfcProtocolIso14443_3a ||
        expected_protocol == NfcProtocolIso14443_4a) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(
            result->detail, sizeof(result->detail), "Public protocol metadata differs");
    } else {
        result->verdict = TumoTagVerdictUnsupported;
        tumotag_copy_text(
            result->detail, sizeof(result->detail), "Detailed comparison is not supported");
    }

    nfc_device_free(observed);
    nfc_device_free(expected);
    return loaded;
}

bool tumotag_compare_lfrfid(
    ProtocolDict* expected,
    ProtocolId expected_protocol,
    ProtocolDict* observed,
    ProtocolId observed_protocol,
    TumoTagResult* result) {
    furi_check(expected);
    furi_check(observed);
    furi_check(result);

    tumotag_result_reset(result, TumoTagMediumLfRfid);
    tumotag_copy_text(
        result->expected_protocol,
        sizeof(result->expected_protocol),
        protocol_dict_get_name(expected, expected_protocol));
    tumotag_copy_text(
        result->observed_protocol,
        sizeof(result->observed_protocol),
        protocol_dict_get_name(observed, observed_protocol));

    if(expected_protocol != observed_protocol) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(result->detail, sizeof(result->detail), "Protocol differs");
        return true;
    }

    const size_t expected_size = protocol_dict_get_data_size(expected, expected_protocol);
    const size_t observed_size = protocol_dict_get_data_size(observed, observed_protocol);
    if(expected_size == 0U || expected_size != observed_size || expected_size > 64U) {
        tumotag_copy_text(result->detail, sizeof(result->detail), "Unsupported RFID data size");
        return false;
    }

    uint8_t expected_data[64];
    uint8_t observed_data[64];
    protocol_dict_get_data(expected, expected_protocol, expected_data, expected_size);
    protocol_dict_get_data(observed, observed_protocol, observed_data, observed_size);
    tumotag_format_hex(
        result->expected_id, sizeof(result->expected_id), expected_data, expected_size);
    tumotag_format_hex(
        result->observed_id, sizeof(result->observed_id), observed_data, observed_size);
    result->compared_units = expected_size;

    for(size_t index = 0U; index < expected_size; index++) {
        if(expected_data[index] != observed_data[index]) {
            result->verdict = TumoTagVerdictDifferent;
            result->mismatched_units = 1U;
            snprintf(
                result->detail, sizeof(result->detail), "Data byte %u differs", (unsigned)index);
            return true;
        }
    }

    result->verdict = TumoTagVerdictVerified;
    tumotag_copy_text(result->detail, sizeof(result->detail), "Protocol and data match");
    return true;
}

bool tumotag_compare_ibutton(
    iButtonProtocols* protocols,
    const iButtonKey* expected,
    const iButtonKey* observed,
    TumoTagResult* result) {
    furi_check(protocols);
    furi_check(expected);
    furi_check(observed);
    furi_check(result);

    tumotag_result_reset(result, TumoTagMediumIButton);
    const iButtonProtocolId expected_protocol = ibutton_key_get_protocol_id(expected);
    const iButtonProtocolId observed_protocol = ibutton_key_get_protocol_id(observed);
    tumotag_copy_text(
        result->expected_protocol,
        sizeof(result->expected_protocol),
        ibutton_protocols_get_name(protocols, expected_protocol));
    tumotag_copy_text(
        result->observed_protocol,
        sizeof(result->observed_protocol),
        ibutton_protocols_get_name(protocols, observed_protocol));

    FuriString* expected_uid = furi_string_alloc();
    FuriString* observed_uid = furi_string_alloc();
    ibutton_protocols_render_uid(protocols, expected, expected_uid);
    ibutton_protocols_render_uid(protocols, observed, observed_uid);
    tumotag_copy_text(
        result->expected_id, sizeof(result->expected_id), furi_string_get_cstr(expected_uid));
    tumotag_copy_text(
        result->observed_id, sizeof(result->observed_id), furi_string_get_cstr(observed_uid));
    furi_string_free(observed_uid);
    furi_string_free(expected_uid);

    if(expected_protocol != observed_protocol) {
        result->verdict = TumoTagVerdictDifferent;
        tumotag_copy_text(result->detail, sizeof(result->detail), "Protocol differs");
        return true;
    }

    iButtonEditableData expected_data;
    iButtonEditableData observed_data;
    ibutton_protocols_get_editable_data(protocols, expected, &expected_data);
    ibutton_protocols_get_editable_data(protocols, observed, &observed_data);
    if(!expected_data.ptr || !observed_data.ptr || expected_data.size == 0U ||
       expected_data.size != observed_data.size || expected_data.size > UINT16_MAX) {
        tumotag_copy_text(result->detail, sizeof(result->detail), "Unsupported iButton data");
        return false;
    }

    result->compared_units = expected_data.size;
    for(size_t index = 0U; index < expected_data.size; index++) {
        if(expected_data.ptr[index] != observed_data.ptr[index]) {
            result->verdict = TumoTagVerdictDifferent;
            result->mismatched_units = 1U;
            snprintf(
                result->detail, sizeof(result->detail), "Data byte %u differs", (unsigned)index);
            return true;
        }
    }

    result->verdict = TumoTagVerdictVerified;
    tumotag_copy_text(result->detail, sizeof(result->detail), "Protocol and data match");
    return true;
}
