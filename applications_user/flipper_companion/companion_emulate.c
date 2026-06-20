// NFC + 125 kHz RFID emulation, adapted from Quac! (actions/action_rfid.c,
// action_nfc.c) by Roberto De Feo, GPL. Trimmed to standalone calls.

#include "companion_emulate.h"

#include <flipper_format/flipper_format.h>
#include <lib/lfrfid/lfrfid_worker.h>
#include <toolbox/protocols/protocol_dict.h>
#include <lfrfid/protocols/lfrfid_protocols.h>

#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>

#define TAG "Companion-Emulate"
#define RFID_FILE_TYPE    "Flipper RFID key"
#define RFID_FILE_VERSION 1

#define SET_ERR(...)                                  \
    do {                                              \
        if(error) furi_string_printf(error, __VA_ARGS__); \
    } while(0)

static void delay_ms(uint32_t total_ms) {
    int32_t remaining = (int32_t)total_ms;
    while(remaining > 0) {
        furi_delay_ms(100);
        remaining -= 100;
    }
}

bool companion_rfid_emulate(
    Storage* storage,
    const char* file_name,
    uint32_t duration_ms,
    FuriString* error) {
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* temp = furi_string_alloc();
    uint32_t version = 0;
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    ProtocolId protocol = PROTOCOL_NO;
    size_t data_size = protocol_dict_get_max_data_size(dict);
    uint8_t* data = malloc(data_size);
    bool ok = false;

    do {
        if(!flipper_format_file_open_existing(fff, file_name)) {
            SET_ERR("RFID: open failed");
            break;
        }
        if(!flipper_format_read_header(fff, temp, &version)) {
            SET_ERR("RFID: bad header");
            break;
        }
        if(strcmp(furi_string_get_cstr(temp), RFID_FILE_TYPE) != 0 || version != RFID_FILE_VERSION) {
            SET_ERR("RFID: type/version");
            break;
        }
        if(!flipper_format_read_string(fff, "Key type", temp)) {
            SET_ERR("RFID: no protocol");
            break;
        }
        protocol = protocol_dict_get_protocol_by_name(dict, furi_string_get_cstr(temp));
        if(protocol == PROTOCOL_NO) {
            SET_ERR("RFID: unknown protocol");
            break;
        }
        size_t required = protocol_dict_get_data_size(dict, protocol);
        if(!flipper_format_read_hex(fff, "Data", data, required)) {
            SET_ERR("RFID: bad data");
            break;
        }
        protocol_dict_set_data(dict, protocol, data, data_size);
        ok = true;
    } while(false);

    if(ok) {
        LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
        lfrfid_worker_start_thread(worker);
        lfrfid_worker_emulate_start(worker, protocol);
        delay_ms(duration_ms);
        lfrfid_worker_stop(worker);
        lfrfid_worker_stop_thread(worker);
        lfrfid_worker_free(worker);
    }

    furi_string_free(temp);
    free(data);
    protocol_dict_free(dict);
    flipper_format_free(fff);
    return ok;
}

bool companion_nfc_emulate(const char* file_name, uint32_t duration_ms, FuriString* error) {
    Nfc* nfc = nfc_alloc();
    NfcDevice* device = nfc_device_alloc();
    bool ok = false;

    if(nfc_device_load(device, file_name)) {
        NfcProtocol protocol = nfc_device_get_protocol(device);
        NfcListener* listener =
            nfc_listener_alloc(nfc, protocol, nfc_device_get_data(device, protocol));
        nfc_listener_start(listener, NULL, NULL);
        delay_ms(duration_ms);
        nfc_listener_stop(listener);
        nfc_listener_free(listener);
        ok = true;
    } else {
        SET_ERR("NFC: load failed");
    }

    nfc_device_clear(device);
    nfc_free(nfc);
    nfc_device_free(device);
    return ok;
}
