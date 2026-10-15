"""Execute Erreka generation, menu routing and saved-field contracts on host."""

from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function, run_c


ROOT = Path(__file__).resolve().parents[2]
PLUGIN = "applications/main/subghz/plugins/add_manually"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def without_includes(source: str) -> str:
    return "\n".join(
        line for line in source.splitlines()
        if not line.startswith(("#include", "#pragma once"))
    )


PRELUDE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
"""


class Erreka868AddManuallyTest(unittest.TestCase):
    def test_generator_keeps_seeded_fields_and_433_profile(self) -> None:
        generator = without_includes(read(f"{PLUGIN}/subghz_gen_info.c"))
        protocol_names = sorted(set(re.findall(r"(subghz_protocol_\w+)\.name", generator)))
        protocols = "\n".join(
            f'static const struct {{ const char* name; }} {name} = {{"{name}"}};'
            for name in protocol_names
        )
        run_c(
            PRELUDE
            + without_includes(read("applications/main/subghz/helpers/subghz_custom_event.h"))
            + without_includes(read("applications/main/subghz/helpers/subghz_gen_info.h"))
            + protocols
            + r"""
static uint32_t sample_key;
#define rand() sample_key
#define SUBGHZ_PROTOCOL_NORD_ICE_NAME "Nord ICE"
static void subghz_txrx_gen_serial_gangqi(uint64_t* value) { *value = 0; }
static void subghz_txrx_gen_key_marantec(uint64_t* value) { *value = 0; }
static uint64_t subghz_protocol_blocks_reverse_key(uint64_t value, uint8_t bits) {
    (void)bits;
    return value;
}
"""
            + generator
            + r"""
int main(void) {
    const uint32_t keys[] = {0, 1, 0xFFFFF, 0x100000, 0x12345678, 0x7FFFFFFF};
    for(size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        sample_key = keys[i];
        GenInfo home = {0};
        subghz_scene_set_type_fill_generation_infos(&home, SetTypeHomeGate433);
        assert(home.type == GenKeeloq && home.freq == 433920000);
        assert(strcmp(home.mod, "AM650") == 0 && strcmp(home.keeloq.manuf, "HomeGate") == 0);
        assert(home.keeloq.serial == (keys[i] & 0x0FFFFFFF));
        assert(home.keeloq.btn == 2 && home.keeloq.cnt == 3);
        GenInfo low = {0}, high = {0};
        subghz_scene_set_type_fill_generation_infos(&low, SetTypeErreka433);
        subghz_scene_set_type_fill_generation_infos(&high, SetTypeErreka868);
        assert(low.type == GenKeeloqSeed && high.type == GenKeeloqSeed);
        assert(low.freq == 433920000 && high.freq == 868350000);
        assert(strcmp(low.mod, "AM650") == 0 && strcmp(high.mod, "AM650") == 0);
        assert(high.keeloq_seed.serial == (keys[i] & 0xFFFFF));
        assert(high.keeloq_seed.seed == (keys[i] & 0xFFFFF));
        assert(high.keeloq_seed.btn == 2 && high.keeloq_seed.cnt == 2);
        assert(strcmp(high.keeloq_seed.manuf, "Erreka") == 0);
        assert(low.keeloq_seed.serial == high.keeloq_seed.serial);
        assert(low.keeloq_seed.seed == high.keeloq_seed.seed);
        assert(low.keeloq_seed.btn == high.keeloq_seed.btn);
        assert(low.keeloq_seed.cnt == high.keeloq_seed.cnt);
        assert(strcmp(low.keeloq_seed.manuf, high.keeloq_seed.manuf) == 0);
    }
    return 0;
}
"""
        )

    def test_menu_keeps_existing_ids_and_groups_both_frequencies(self) -> None:
        source = read(f"{PLUGIN}/subghz_scene_set_type.c")
        menu_start = source.index("static const char* submenu_names[")
        menu_end = source.index("\n};", menu_start) + len("\n};")
        production = source[menu_start:menu_end]
        production += "\n" + function(source, "void subghz_scene_set_type_on_enter(")
        run_c(
            PRELUDE
            + without_includes(read("applications/main/subghz/helpers/subghz_custom_event.h"))
            + r"""
_Static_assert(SetTypeErreka433 == 5, "Existing Erreka selection ID changed");
_Static_assert(SetTypeSomfyTelis == 6, "Existing downstream selection IDs changed");
_Static_assert(SetTypeNiceOCode_433_92 == 68, "Existing Nice O-Code ID changed");
_Static_assert(SetTypeSecPlus_v2_433_00 == 100, "Existing final selection ID changed");
_Static_assert(SetTypeErreka868 == 101, "New selection must be appended");
_Static_assert(SetTypeHomeGate433 == 102, "HomeGate must not shift existing selections");
typedef struct { const char* names[SetTypeMAX]; uint32_t ids[SetTypeMAX];
                 size_t count; uint32_t selected; } Submenu;
typedef struct { Submenu* submenu; void* scene_manager; void* view_dispatcher; } SubGhz;
enum { SubGhzSceneSetType, SubGhzViewIdMenu };
static void subghz_scene_set_type_submenu_callback(void* context, uint32_t index) {
    (void)context; (void)index;
}
static void submenu_add_item(Submenu* menu, const char* name, uint32_t index,
                             void (*callback)(void*, uint32_t), void* context) {
    assert(name && *name && index < SetTypeMAX && menu->count < SetTypeMAX);
    assert(callback == subghz_scene_set_type_submenu_callback && context);
    menu->names[menu->count] = name;
    menu->ids[menu->count++] = index;
}
static uint32_t scene_manager_get_scene_state(void* manager, int scene) {
    (void)manager; (void)scene;
    return SetTypeErreka868;
}
static void submenu_set_selected_item(Submenu* menu, uint32_t selected) {
    menu->selected = selected;
}
static void view_dispatcher_switch_to_view(void* dispatcher, int view) {
    (void)dispatcher; assert(view == SubGhzViewIdMenu);
}
"""
            + production
            + r"""
int main(void) {
    Submenu menu = {0};
    SubGhz app = {.submenu = &menu};
    subghz_scene_set_type_on_enter(&app);
    assert(menu.count == SetTypeMAX && menu.selected == SetTypeErreka868);
    bool seen[SetTypeMAX] = {0};
    for(size_t i = 0; i < menu.count; i++) {
        assert(!seen[menu.ids[i]]);
        seen[menu.ids[i]] = true;
        if(menu.ids[i] == SetTypeErreka433) {
            assert(i + 1 < menu.count && menu.ids[i + 1] == SetTypeErreka868);
            assert(strcmp(menu.names[i], "Erreka 433MHz") == 0);
            assert(strcmp(menu.names[i + 1], "Erreka 868MHz") == 0);
        }
    }
    for(size_t i = 0; i < SetTypeMAX; i++) assert(seen[i]);
    return 0;
}
"""
        )

    def test_generator_reports_metadata_write_failures(self) -> None:
        production = function(
            read(f"{PLUGIN}/subghz_txrx_create_protocol_key.c"),
            "bool subghz_txrx_gen_keeloq_seed_protocol(",
        )
        run_c(PRELUDE + r"""
#define SUBGHZ_PROTOCOL_KEELOQ_NAME "KeeLoq"
typedef struct { bool fail_seed, fail_manufacture, has_seed, has_manufacture;
                 uint8_t seed[4]; } FlipperFormat;
typedef struct { void* environment; FlipperFormat* fff_data; void* preset; } SubGhzTxRx;
typedef int SubGhzTransmitter;
static SubGhzTransmitter transmitter;
static unsigned allocated, freed;
static SubGhzTransmitter* subghz_transmitter_alloc_init(void* environment, const char* name) {
    (void)environment; assert(strcmp(name, "KeeLoq") == 0); allocated++;
    return &transmitter;
}
static void subghz_txrx_set_preset(SubGhzTxRx* txrx, const char* preset,
                                  uint32_t frequency, const void* data, size_t size) {
    (void)txrx; (void)data; (void)size;
    assert(strcmp(preset, "AM650") == 0 && frequency == 868350000);
}
static void* subghz_transmitter_get_protocol_instance(SubGhzTransmitter* tx) { return tx; }
static bool subghz_protocol_keeloq_seed_create_data(
    void* instance, FlipperFormat* format, uint32_t serial, uint8_t button,
    uint16_t counter, uint32_t seed, const char* manufacture, void* preset) {
    (void)instance; (void)format; (void)preset;
    assert(serial == 0x12345 && button == 2 && counter == 2 && seed == 0x12345);
    assert(strcmp(manufacture, "Erreka") == 0);
    return true;
}
static bool flipper_format_write_hex(FlipperFormat* format, const char* key,
                                    const uint8_t* seed, size_t size) {
    assert(strcmp(key, "Seed") == 0 && size == 4);
    if(format->fail_seed) return false;
    memcpy(format->seed, seed, size); format->has_seed = true;
    return true;
}
static bool flipper_format_write_string_cstr(FlipperFormat* format, const char* key,
                                            const char* manufacture) {
    assert(strcmp(key, "Manufacture") == 0 && strcmp(manufacture, "Erreka") == 0);
    if(format->fail_manufacture) return false;
    format->has_manufacture = true;
    return true;
}
static void subghz_transmitter_free(SubGhzTransmitter* tx) { assert(tx == &transmitter); freed++; }
""" + production + r"""
int main(void) {
    for(unsigned failure = 0; failure < 3; failure++) {
        FlipperFormat format = {.fail_seed = failure == 1, .fail_manufacture = failure == 2};
        SubGhzTxRx txrx = {.fff_data = &format};
        bool success = subghz_txrx_gen_keeloq_seed_protocol(
            &txrx, "AM650", 868350000, 0x12345, 2, 2, 0x12345, "Erreka");
        assert(success == (failure == 0));
        assert(allocated == failure + 1 && freed == allocated);
        if(success) {
            const uint8_t expected[] = {0, 1, 0x23, 0x45};
            assert(format.has_seed && format.has_manufacture);
            assert(memcmp(format.seed, expected, sizeof(expected)) == 0);
        }
    }
    return 0;
}
""")

    def test_saved_seed_roundtrip_and_write_failure(self) -> None:
        source = read("lib/subghz/protocols/keeloq.c")
        production = function(source, "SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(")
        production += "\nSubGhzProtocolStatus\n" + function(
            source, "    subghz_protocol_decoder_keeloq_deserialize("
        )
        run_c(PRELUDE + r"""
typedef enum { SubGhzProtocolStatusOk, SubGhzProtocolStatusError } SubGhzProtocolStatus;
typedef struct { uint32_t seed, data_count_bit; uint64_t data; } SubGhzBlockGeneric;
typedef struct { uint32_t frequency; const char* name; } SubGhzRadioPreset;
typedef struct { const char* mfname; } Keystore;
typedef struct { char text[32]; } FuriString;
typedef struct { SubGhzBlockGeneric generic; Keystore* keystore;
                 const char* manufacture_name; FuriString* manufacture_from_file;
               } SubGhzProtocolDecoderKeeloq;
typedef struct { SubGhzBlockGeneric generic; uint32_t frequency; const char* preset;
                 bool has_seed, fail_seed; uint8_t seed[4]; char manufacture[32];
               } FlipperFormat;
static const struct { uint32_t min_count_bit_for_found; } subghz_protocol_keeloq_const = {64};
static uint8_t keeloq_counter_mode;
static SubGhzProtocolStatus subghz_block_generic_serialize(
    const SubGhzBlockGeneric* generic, FlipperFormat* format, const SubGhzRadioPreset* preset) {
    format->generic = *generic;
    format->generic.seed = 0; /* Only an explicit Seed field survives storage. */
    format->frequency = preset->frequency;
    format->preset = preset->name;
    return SubGhzProtocolStatusOk;
}
static SubGhzProtocolStatus subghz_block_generic_deserialize(
    SubGhzBlockGeneric* generic, const FlipperFormat* format) {
    *generic = format->generic;
    return SubGhzProtocolStatusOk;
}
static void subghz_protocol_keeloq_check_remote_controller(
    SubGhzBlockGeneric* generic, Keystore* keystore, const char** name) {
    (void)generic; (void)keystore; (void)name;
}
static bool flipper_format_write_hex(FlipperFormat* format, const char* key,
                                    const uint8_t* bytes, size_t count) {
    assert(strcmp(key, "Seed") == 0 && count == 4);
    if(format->fail_seed) return false;
    memcpy(format->seed, bytes, count); format->has_seed = true;
    return true;
}
static bool flipper_format_read_hex(const FlipperFormat* format, const char* key,
                                   uint8_t* bytes, size_t count) {
    assert(strcmp(key, "Seed") == 0 && count == 4);
    if(!format->has_seed) return false;
    memcpy(bytes, format->seed, count);
    return true;
}
static bool flipper_format_write_string_cstr(FlipperFormat* format, const char* key,
                                            const char* value) {
    assert(strcmp(key, "Manufacture") == 0);
    assert(strlen(value) < sizeof(format->manufacture));
    strcpy(format->manufacture, value);
    return true;
}
static bool flipper_format_update_string_cstr(FlipperFormat* format, const char* key,
                                             const char* value) {
    return flipper_format_write_string_cstr(format, key, value);
}
static bool flipper_format_read_string(const FlipperFormat* format, const char* key,
                                      FuriString* value) {
    assert(strcmp(key, "Manufacture") == 0);
    if(!*format->manufacture) return false;
    strcpy(value->text, format->manufacture);
    return true;
}
static const char* furi_string_get_cstr(const FuriString* value) { return value->text; }
static bool flipper_format_rewind(FlipperFormat* format) { (void)format; return true; }
static bool flipper_format_read_uint32(FlipperFormat* format, const char* key,
                                      uint32_t* value, size_t count) {
    (void)format; (void)key; (void)value; (void)count; return false;
}
""" + production + r"""
int main(void) {
    const char* names[] = {"Erreka", "BFT", "Monarch"};
    const uint32_t seeds[] = {0, 1, 0x12345678, UINT32_MAX};
    const uint32_t frequencies[] = {433920000, 868350000};
    Keystore keystore = {0};
    for(size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        for(size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); s++) {
            for(size_t f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++) {
                SubGhzRadioPreset preset = {frequencies[f], "AM650"};
                SubGhzProtocolDecoderKeeloq original = {
                    .generic = {.seed = seeds[s], .data_count_bit = 64,
                                .data = UINT64_C(0x12345678ABCDEF01)},
                    .keystore = &keystore, .manufacture_name = names[n]};
                FlipperFormat saved = {0};
                assert(subghz_protocol_decoder_keeloq_serialize(&original, &saved, &preset) ==
                       SubGhzProtocolStatusOk);
                assert(saved.has_seed && saved.frequency == frequencies[f]);
                assert(strcmp(saved.preset, "AM650") == 0);
                FuriString name = {0};
                SubGhzProtocolDecoderKeeloq restored = {
                    .keystore = &keystore, .manufacture_from_file = &name};
                assert(subghz_protocol_decoder_keeloq_deserialize(&restored, &saved) ==
                       SubGhzProtocolStatusOk);
                assert(restored.generic.seed == seeds[s]);
                assert(restored.generic.data == original.generic.data);
                assert(strcmp(restored.manufacture_name, names[n]) == 0);
                FlipperFormat failed = {.fail_seed = true};
                assert(subghz_protocol_decoder_keeloq_serialize(&original, &failed, &preset) ==
                       SubGhzProtocolStatusError);
                assert(!failed.has_seed && !*failed.manufacture);
            }
        }
    }
    SubGhzProtocolDecoderKeeloq unseeded = {.keystore = &keystore,
                                         .manufacture_name = "Sommer"};
    SubGhzRadioPreset preset = {433920000, "AM650"};
    FlipperFormat saved = {0};
    assert(subghz_protocol_decoder_keeloq_serialize(&unseeded, &saved, &preset) ==
           SubGhzProtocolStatusOk && !saved.has_seed);
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
