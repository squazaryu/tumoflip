"""Execute adapted FeliCa command paths with ASan/UBSan and synthetic dumps."""

from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native


ROOT = Path(__file__).resolve().parents[2]
BASE = "lib/nfc/protocols/felica/"


def read(name):
    return (ROOT / BASE / name).read_text(encoding="utf-8")


def declarations(text):
    return "\n".join(line for line in text.splitlines()
                     if not line.startswith(("#include", "#pragma once")))


PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define FURI_PACKED __attribute__((packed))
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define STUB static __attribute__((unused))
typedef struct { int unused; } mbedtls_des3_context;
typedef struct { void* items; size_t count, item_size; } SimpleArray;
typedef struct { uint8_t bytes[256]; size_t size; } BitBuffer;
typedef struct { int unused; } Nfc;
typedef struct FelicaListener FelicaListener;
typedef enum {NfcCommandContinue, NfcCommandReset, NfcCommandStop} NfcCommand;
typedef struct { int protocol; void* event_data; } NfcGenericEvent;
typedef NfcCommand (*NfcGenericCallback)(NfcGenericEvent, void*);
enum {NfcProtocolInvalid, NfcModeListener, NfcTechFelica};
enum {NfcEventTypeFieldOn, NfcEventTypeListenerActivated, NfcEventTypeFieldOff, NfcEventTypeRxEnd};
typedef struct { int type; struct {BitBuffer* buffer;} data; } NfcEvent;
STUB uint32_t simple_array_get_count(const SimpleArray* array) {
    assert(array); return (uint32_t)array->count;
}
STUB const void* simple_array_cget(const SimpleArray* array, size_t index) {
    assert(array && index < array->count);
    return (const uint8_t*)array->items + index * array->item_size;
}
STUB void* simple_array_get(SimpleArray* array, size_t index) {
    return (void*)simple_array_cget(array, index);
}
STUB void bit_buffer_reset(BitBuffer* buffer) { buffer->size = 0; }
STUB void bit_buffer_append_bytes(BitBuffer* buffer, const uint8_t* bytes, size_t size) {
    assert(size <= sizeof(buffer->bytes)); memcpy(buffer->bytes, bytes, size); buffer->size = size;
}
'''


def support():
    public = declarations(read("felica.h").split("extern const NfcDeviceBase")[0])
    internal = declarations(read("felica_listener_i.h").split("/** Resets card")[0])
    return PRELUDE + public + internal + r'''
_Static_assert(FelicaErrorNone == 0 && FelicaErrorFeatureUnsupported == 9,
               "Existing public error IDs must stay stable");
_Static_assert(FelicaErrorIgnore == 10, "New error must be appended");
STUB FelicaError felica_listener_frame_exchange(FelicaListener* instance, BitBuffer* buffer) {
    (void)instance; (void)buffer; return FelicaErrorNone;
}
'''


def idm_functions():
    source = read("felica_listener_i.c")
    return "\n".join(function(source, signature) for signature in (
        "static FelicaIDm\n    felica_listener_get_system_idm(",
        "FelicaIDm felica_listener_get_current_idm(",
        "bool felica_listener_select_system_by_idm(",
    ))


class FelicaCommandsNativeTest(unittest.TestCase):
    def test_extended_blocks_and_atomic_writes(self):
        source = read("felica_listener.c")
        production = idm_functions()
        for signature in (
            "static const FelicaSystem* felica_listener_get_current_system(",
            "static FelicaSystem* felica_listener_get_current_system_mut(",
            "static const FelicaService*\n    felica_listener_get_service(",
            "static FelicaError felica_listener_send_status_response(",
            "static FelicaError felica_listener_command_handler_standard_read(",
            "static FelicaError felica_listener_command_handler_standard_write(",
        ):
            production += "\n" + function(source, signature)
        production += "\n" + function(read("felica_listener_i.c"),
                                       "static bool felica_listener_validate_standard_request(")
        native(support() + r'''
#define FELICA_LISTENER_MAX_BUFFER_SIZE 128
#define FELICA_STANDARD_READ_BLOCK_MAX 7
#define FELICA_LISTENER_RESPONSE_CODE_READ 7
#define FELICA_LISTENER_RESPONSE_CODE_WRITE 9
#define FELICA_BLOCK_LIST_ELEMENT_HEADER_SHORT_MASK 0x80
#define FELICA_BLOCK_LIST_ELEMENT_HEADER_SERVICE_INDEX_MASK 0x0F
''' + production + r'''
static size_t request(uint8_t* out, bool write, uint16_t first, uint16_t second,
                      uint8_t count) {
    memset(out, 0, 128); out[1] = write ? 8 : 6; out[10] = 1; out[11] = 9;
    out[13] = count; size_t size = 14;
    const uint16_t indices[] = {first, second};
    for(uint8_t i = 0; i < count; i++) {
        out[size++] = 0; out[size++] = indices[i] & 0xFF; out[size++] = indices[i] >> 8;
    }
    if(write) { memset(out + size, 0x77, count * 16); size += count * 16; }
    out[0] = (uint8_t)size; return size;
}
int main(void) {
    FelicaService service = {.code = 9};
    SimpleArray services = {&service, 1, sizeof(service)};
    FelicaPublicBlock blocks[2] = {{.service_code = 9, .block_idx = 1},
                                  {.service_code = 9, .block_idx = 2}};
    memset(blocks[0].block.data, 0x11, 16); memset(blocks[1].block.data, 0x33, 16);
    SimpleArray public_blocks = {blocks, 2, sizeof(blocks[0])};
    FelicaSystem system = {.services = &services, .public_blocks = &public_blocks};
    SimpleArray systems = {&system, 1, sizeof(system)};
    FelicaData data = {.workflow_type = FelicaStandard, .systems = &systems};
    BitBuffer tx = {0}; FelicaListener listener = {.data = &data, .tx_buffer = &tx};
    uint8_t raw[128]; size_t size = request(raw, false, 0x101, 0, 1);
    assert(felica_listener_validate_standard_request((void*)raw, size, false));
    felica_listener_command_handler_standard_read(&listener, (void*)raw);
    // The dump's 8-bit index must not alias a full 16-bit wire address.
    assert(tx.size == 12 && tx.bytes[10] != 0 && tx.bytes[11] == 0xA8);
    size = request(raw, false, 2, 0, 1);
    felica_listener_command_handler_standard_read(&listener, (void*)raw);
    assert(tx.size == 29 && tx.bytes[10] == 0 && tx.bytes[11] == 0);
    for(size_t i = 13; i < tx.size; i++) assert(tx.bytes[i] == 0x33);
    // A missing later target must not change the earlier valid block.
    size = request(raw, true, 1, 3, 2);
    assert(felica_listener_validate_standard_request((void*)raw, size, true));
    felica_listener_command_handler_standard_write(&listener, (void*)raw);
    assert(tx.bytes[10] != 0 && tx.bytes[11] == 0xA8);
    for(size_t i = 0; i < 16; i++) assert(blocks[0].block.data[i] == 0x11);
    // Exact request shape remains fail-closed for every truncated prefix.
    for(size_t length = 0; length < size; length++)
        assert(!felica_listener_validate_standard_request((void*)raw, length, true));
    size = request(raw, true, 1, 2, 2);
    felica_listener_command_handler_standard_write(&listener, (void*)raw);
    assert(tx.bytes[10] == 0 && tx.bytes[11] == 0);
    for(size_t i = 0; i < 16; i++) assert(blocks[0].block.data[i] == 0x77 &&
                                       blocks[1].block.data[i] == 0x77);
    service.attr = FELICA_SERVICE_ATTRIBUTE_READ_ONLY;
    memset(blocks[0].block.data, 0x44, 16);
    felica_listener_command_handler_standard_write(&listener, (void*)raw);
    assert(tx.bytes[11] == 0xA6);
    for(size_t i = 0; i < 16; i++) assert(blocks[0].block.data[i] == 0x44);
    return 0;
}
''')

    def test_emulation_preserves_pmm_and_reports_saved_system(self):
        source = read("felica_listener.c")
        production = function(source, "FelicaListener* felica_listener_alloc(")
        production += "\n" + function(source, "void felica_listener_free(")
        native(support() + r'''
#define FELICA_LISTENER_MAX_BUFFER_SIZE 128
static uint8_t hardware_pmm[8]; static uint16_t hardware_system;
static BitBuffer* bit_buffer_alloc(size_t size) { (void)size; return calloc(1, sizeof(BitBuffer)); }
static void bit_buffer_free(BitBuffer* buffer) { free(buffer); }
static void mbedtls_des3_init(mbedtls_des3_context* context) { (void)context; }
static void nfc_set_fdt_listen_fc(Nfc* nfc, uint32_t value) { (void)nfc; (void)value; }
static void nfc_config(Nfc* nfc, int mode, int technology) { (void)nfc; (void)mode; (void)technology; }
static void nfc_felica_listener_set_sensf_res_data(Nfc* nfc, const uint8_t* idm, size_t idm_size,
                                                const uint8_t* pmm, size_t pmm_size, uint16_t system) {
    (void)nfc; (void)idm; assert(idm_size == 8 && pmm_size == 8);
    memcpy(hardware_pmm, pmm, 8); hardware_system = system;
}
''' + production + r'''
int main(void) {
    FelicaSystem system = {.system_code = 0x1234};
    SimpleArray systems = {&system, 1, sizeof(system)};
    FelicaData data = {.workflow_type = FelicaStandard, .systems = &systems,
                       .pmm = {.data = {0x31, 5, 1, 2, 3, 4, 5, 6}}};
    FelicaPMm original = data.pmm; Nfc nfc = {0};
    FelicaListener* listener = felica_listener_alloc(&nfc, &data);
    assert(memcmp(&data.pmm, &original, sizeof(original)) == 0);
    assert(hardware_system == 0x3412);
    assert(hardware_pmm[0] == 0x31 && hardware_pmm[1] == 5);
    for(size_t i = 2; i < 8; i++) assert(hardware_pmm[i] == 0xFF);
    felica_listener_free(listener);
    assert(memcmp(&data.pmm, &original, sizeof(original)) == 0);
    return 0;
}
''')

    def test_standard_polling_only_reports_saved_systems(self):
        source = read("felica_listener.c")
        production = function(source, "static bool felica_listener_check_system_code(")
        production += "\n" + function(source, "static uint16_t felica_listener_get_response_system_code(")
        native(support() + r'''
#define FELICA_LISTENER_SYSTEM_CODE_NDEF (__builtin_bswap16(0x12FCU))
#define FELICA_LISTENER_SYSTEM_CODE_LITES (__builtin_bswap16(0x88B4U))
''' + production + r'''
int main(void) {
    FelicaSystem system = {.system_code = 3};
    SimpleArray systems = {&system, 1, sizeof(system)};
    FelicaData data = {.workflow_type = FelicaStandard, .systems = &systems};
    data.data.fs.mc.data[FELICA_MC_SYS_OP] = 1;
    FelicaListener listener = {.data = &data};
    FelicaListenerGenericRequest request = {0}; uint8_t selected = 99;
    const uint16_t virtual_codes[] = {FELICA_LISTENER_SYSTEM_CODE_NDEF, FELICA_LISTENER_SYSTEM_CODE_LITES};
    for(size_t i = 0; i < COUNT_OF(virtual_codes); i++) {
        request.polling.system_code = virtual_codes[i];
        assert(felica_listener_get_response_system_code(&listener, &request, &selected) == 0xFFFF);
    }
    request.polling.system_code = __builtin_bswap16(3);
    assert(felica_listener_get_response_system_code(&listener, &request, &selected) == __builtin_bswap16(3));
    assert(selected == 0);
    data.workflow_type = FelicaLite;
    for(size_t i = 0; i < COUNT_OF(virtual_codes); i++) {
        request.polling.system_code = virtual_codes[i];
        assert(felica_listener_get_response_system_code(&listener, &request, &selected) == virtual_codes[i]);
    }
    return 0;
}
''')

    def test_rejected_frames_rearm_and_system_ids_keep_base_identity(self):
        internal = read("felica_listener_i.c")
        production = idm_functions() + "\n" + function(internal, "bool felica_listener_validate_request(")
        production += "\n" + function(read("felica_listener.c"), "NfcCommand felica_listener_run(")
        native(support() + r'''
#define FELICA_CRC_SIZE 2
#define FELICA_LISTENER_CMD_POLLING 0
static bool crc_valid = true;
static unsigned dispatches;
static size_t bit_buffer_get_size_bytes(const BitBuffer* buffer) { return buffer->size; }
static uint8_t* bit_buffer_get_data(BitBuffer* buffer) { return buffer->bytes; }
static bool felica_crc_check(const BitBuffer* buffer) { (void)buffer; return crc_valid; }
static void felica_listener_reset(FelicaListener* listener) { listener->current_system_idx = 0; }
static void nfc_felica_listener_timer_anticol_start(Nfc* nfc, unsigned slot) { (void)nfc; (void)slot; }
static FelicaError felica_listener_process_system_code(FelicaListener* listener,
                                                       const FelicaListenerGenericRequest* request) {
    (void)listener; (void)request; return FelicaErrorIgnore;
}
static FelicaError felica_listener_process_request(FelicaListener* listener,
                                                  const FelicaListenerGenericRequest* request) {
    (void)listener; dispatches++;
    return request->header.code == 4 ? FelicaErrorNone : FelicaErrorFeatureUnsupported;
}
static bool felica_listener_validate_standard_request(const FelicaListenerGenericRequest* r,
                                                       size_t size, bool write) {
    (void)r; (void)size; (void)write; return false;
}
static uint8_t felica_listener_get_block_list_item_count_size(FelicaListener* l, FelicaListenerRequest* r) {
    (void)l; (void)r; return 0;
}
static bool felica_listener_test_block_list_size_bounds(const FelicaListenerGenericRequest* r) {
    (void)r; return false;
}
static bool felica_listener_check_write_request_data_size(const FelicaListener* l,
                                                         const FelicaListenerRequest* r, uint8_t count) {
    (void)l; (void)r; (void)count; return false;
}
''' + production + r'''
static NfcCommand deliver(FelicaListener* listener, uint8_t command, uint8_t idm_first,
                           size_t length, bool good_crc) {
    BitBuffer frame = {.size = length}; frame.bytes[0] = 10; frame.bytes[1] = command;
    memcpy(frame.bytes + 2, listener->data->idm.data, 8); frame.bytes[2] = idm_first;
    crc_valid = good_crc;
    NfcEvent event = {.type = NfcEventTypeRxEnd, .data = {.buffer = &frame}};
    NfcCommand result = felica_listener_run((NfcGenericEvent){NfcProtocolInvalid, &event}, listener);
    assert(frame.size == 0); return result;
}
int main(void) {
    FelicaSystem items[2] = {0}; SimpleArray systems = {items, 2, sizeof(items[0])};
    FelicaData data = {.workflow_type = FelicaStandard, .systems = &systems,
                       .idm = {.data = {0xE1, 2, 3, 4, 5, 6, 7, 8}}};
    FelicaListener listener = {.data = &data};
    assert(deliver(&listener, 0xFE, 0xE1, 12, true) == NfcCommandReset);
    unsigned previous = dispatches;
    assert(deliver(&listener, 4, 0x99, 12, true) == NfcCommandReset);
    assert(deliver(&listener, 4, 0xE1, 12, false) == NfcCommandReset);
    for(size_t size = 0; size < 12; size++)
        assert(deliver(&listener, 4, 0xE1, size, true) == NfcCommandReset);
    assert(dispatches == previous);
    assert(deliver(&listener, 4, 0x11, 12, true) == NfcCommandContinue);
    assert(listener.current_system_idx == 1 && data.idm.data[0] == 0xE1);
    NfcEvent activation = {.type = NfcEventTypeListenerActivated};
    felica_listener_run((NfcGenericEvent){NfcProtocolInvalid, &activation}, &listener);
    assert(listener.current_system_idx == 0);
    FelicaIDm base = felica_listener_get_current_idm(&listener);
    assert(base.data[0] == 0xE1);
    assert(deliver(&listener, 4, 0xE1, 12, true) == NfcCommandContinue);
    return 0;
}
''')

    def test_timeouts_follow_pmm_without_overflow(self):
        private = read("felica_poller_i.h")
        end = private.index("} FelicaPollerCommandClass;") + len("} FelicaPollerCommandClass;")
        begin = private.rfind("typedef enum {", 0, end)
        production = function(read("felica_poller_i.c"), "uint32_t felica_poller_get_fwt(")
        native(PRELUDE + r'''
#define FELICA_POLLER_POLLING_FWT 200000U
typedef struct { struct {uint8_t data[8];} pmm; } Data;
typedef struct {Data* data;} FelicaPoller;
''' + private[begin:end] + production + r'''
int main(void) {
    Data data = {0}; FelicaPoller poller = {.data = &data};
    const uint8_t units[] = {0, 1, 7, 32, 255};
    for(unsigned command = 0; command < 6; command++) {
        for(unsigned param = 0; param < 256; param++) {
            data.pmm.data[2 + command] = param;
            for(size_t i = 0; i < COUNT_OF(units); i++) {
                uint64_t factor = 1;
                for(unsigned exponent = 0; exponent < param / 64; exponent++) factor *= 4;
                uint64_t expected = 4096ULL * ((param % 8 + 1) + units[i] * ((param / 8) % 8 + 1)) * factor;
                if(expected < 200000) expected = 200000;
                assert(expected <= UINT32_MAX);
                assert(felica_poller_get_fwt(&poller, command, units[i]) == expected);
            }
        }
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
