#include <furi.h>
#include <furi_hal.h>
#include "../test.h" // IWYU pragma: keep
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <lib/subghz/protocols/faac_slh.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/protocols/kia_v1.h>
#include <lib/subghz/environment.h>
#include <gui/modules/loading.h>
#include <flipper_format/flipper_format_i.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_configs.h>

#define TAG "SubGhzTest"

#define KEYSTORE_DIR_NAME       EXT_PATH("subghz/assets/keeloq_mfcodes")
#define CAME_ATOMO_DIR_NAME     EXT_PATH("subghz/assets/came_atomo")
#define NICE_FLOR_S_DIR_NAME    EXT_PATH("subghz/assets/nice_flor_s")
#define ALUTECH_AT_4N_DIR_NAME  EXT_PATH("subghz/assets/alutech_at_4n")
#define TEST_RANDOM_DIR_NAME    EXT_PATH("unit_tests/subghz/test_random_raw.sub")
#define TEST_RANDOM_COUNT_PARSE 328
#define TEST_TIMEOUT            10000

static SubGhzEnvironment* environment_handler;
static SubGhzReceiver* receiver_handler;
//static SubGhzTransmitter* transmitter_handler;
static SubGhzFileEncoderWorker* file_worker_encoder_handler;
static uint16_t subghz_test_decoder_count = 0;

static void subghz_test_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    UNUSED(context);
    FuriString* text;
    text = furi_string_alloc();
    subghz_protocol_decoder_base_get_string(decoder_base, text);
    subghz_receiver_reset(receiver_handler);
    FURI_LOG_T(TAG, "\r\n%s", furi_string_get_cstr(text));
    furi_string_free(text);
    subghz_test_decoder_count++;
}

static void subghz_test_init(void) {
    environment_handler = subghz_environment_alloc();
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        environment_handler, NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        environment_handler, ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_protocol_registry(
        environment_handler, (void*)&subghz_protocol_registry);

    subghz_devices_init();

    receiver_handler = subghz_receiver_alloc_init(environment_handler);
    subghz_receiver_set_filter(receiver_handler, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(receiver_handler, subghz_test_rx_callback, NULL);
}

static void subghz_test_deinit(void) {
    subghz_devices_deinit();
    subghz_receiver_free(receiver_handler);
    subghz_environment_free(environment_handler);
}

static bool subghz_decoder_test(const char* path, const char* name_decoder) {
    subghz_test_decoder_count = 0;
    uint32_t test_start = furi_get_tick();

    SubGhzProtocolDecoderBase* decoder =
        subghz_receiver_search_decoder_base_by_name(receiver_handler, name_decoder);

    if(decoder) {
        file_worker_encoder_handler = subghz_file_encoder_worker_alloc();
        if(subghz_file_encoder_worker_start(file_worker_encoder_handler, path, NULL)) {
            // the worker needs a file in order to open and read part of the file
            furi_delay_ms(100);

            LevelDuration level_duration;
            while(furi_get_tick() - test_start < TEST_TIMEOUT) {
                level_duration =
                    subghz_file_encoder_worker_get_level_duration(file_worker_encoder_handler);
                if(!level_duration_is_reset(level_duration)) {
                    bool level = level_duration_get_level(level_duration);
                    uint32_t duration = level_duration_get_duration(level_duration);
                    // Yield, to load data inside the worker
                    furi_thread_yield();
                    decoder->protocol->decoder->feed(decoder, level, duration);
                } else {
                    break;
                }
            }
            furi_delay_ms(10);
        }
        if(subghz_file_encoder_worker_is_running(file_worker_encoder_handler)) {
            subghz_file_encoder_worker_stop(file_worker_encoder_handler);
        }
        subghz_file_encoder_worker_free(file_worker_encoder_handler);
    }
    FURI_LOG_T(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT) {
        printf("Test decoder %s ERROR TimeOut\r\n", name_decoder);
        return false;
    } else {
        return subghz_test_decoder_count ? true : false;
    }
}

static bool subghz_decode_random_test(const char* path) {
    subghz_test_decoder_count = 0;
    subghz_receiver_reset(receiver_handler);
    uint32_t test_start = furi_get_tick();

    file_worker_encoder_handler = subghz_file_encoder_worker_alloc();
    if(subghz_file_encoder_worker_start(file_worker_encoder_handler, path, NULL)) {
        // the worker needs a file in order to open and read part of the file
        furi_delay_ms(100);

        LevelDuration level_duration;
        while(furi_get_tick() - test_start < TEST_TIMEOUT * 10) {
            level_duration =
                subghz_file_encoder_worker_get_level_duration(file_worker_encoder_handler);
            if(!level_duration_is_reset(level_duration)) {
                bool level = level_duration_get_level(level_duration);
                uint32_t duration = level_duration_get_duration(level_duration);
                // Yield, to load data inside the worker
                furi_thread_yield();
                subghz_receiver_decode(receiver_handler, level, duration);
            } else {
                break;
            }
        }
        furi_delay_ms(10);
        if(subghz_file_encoder_worker_is_running(file_worker_encoder_handler)) {
            subghz_file_encoder_worker_stop(file_worker_encoder_handler);
        }
        subghz_file_encoder_worker_free(file_worker_encoder_handler);
    }
    FURI_LOG_D(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT * 10) {
        printf("Random test ERROR TimeOut\r\n");
        return false;
    } else if(subghz_test_decoder_count == TEST_RANDOM_COUNT_PARSE) {
        return true;
    } else {
        return false;
    }
}

static bool subghz_encoder_test(const char* path) {
    subghz_test_decoder_count = 0;
    uint32_t test_start = furi_get_tick();
    FuriString* temp_str;
    temp_str = furi_string_alloc();
    bool file_load = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(fff_data_file, path)) {
            FURI_LOG_E(TAG, "Error open file %s", path);
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            FURI_LOG_E(TAG, "Missing Preset");
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        file_load = true;
    } while(false);
    if(file_load) {
        SubGhzTransmitter* transmitter =
            subghz_transmitter_alloc_init(environment_handler, furi_string_get_cstr(temp_str));
        subghz_transmitter_deserialize(transmitter, fff_data_file);

        SubGhzProtocolDecoderBase* decoder = subghz_receiver_search_decoder_base_by_name(
            receiver_handler, furi_string_get_cstr(temp_str));

        if(decoder) {
            LevelDuration level_duration;
            while(furi_get_tick() - test_start < TEST_TIMEOUT) {
                level_duration = subghz_transmitter_yield(transmitter);
                if(!level_duration_is_reset(level_duration)) {
                    bool level = level_duration_get_level(level_duration);
                    uint32_t duration = level_duration_get_duration(level_duration);
                    decoder->protocol->decoder->feed(decoder, level, duration);
                } else {
                    break;
                }
            }
            furi_delay_ms(10);
        }
        subghz_transmitter_free(transmitter);
    }
    flipper_format_free(fff_data_file);
    FURI_LOG_T(TAG, "Decoder count parse %d", subghz_test_decoder_count);
    if(furi_get_tick() - test_start > TEST_TIMEOUT) {
        printf("Test encoder %s ERROR TimeOut\r\n", furi_string_get_cstr(temp_str));
        subghz_test_decoder_count = 0;
    }
    furi_string_free(temp_str);

    return subghz_test_decoder_count ? true : false;
}

MU_TEST(subghz_keystore_test) {
    mu_assert(
        subghz_environment_load_keystore(environment_handler, KEYSTORE_DIR_NAME),
        "Test keystore error");
}

typedef enum {
    SubGhzHalAsyncTxTestTypeNormal,
    SubGhzHalAsyncTxTestTypeInvalidStart,
    SubGhzHalAsyncTxTestTypeInvalidMid,
    SubGhzHalAsyncTxTestTypeInvalidEnd,
    SubGhzHalAsyncTxTestTypeResetStart,
    SubGhzHalAsyncTxTestTypeResetMid,
    SubGhzHalAsyncTxTestTypeResetEnd,
} SubGhzHalAsyncTxTestType;

typedef struct {
    SubGhzHalAsyncTxTestType type;
    size_t pos;
} SubGhzHalAsyncTxTest;

#define SUBGHZ_HAL_TEST_DURATION 3

static LevelDuration subghz_hal_async_tx_test_yield(void* context) {
    SubGhzHalAsyncTxTest* test = context;
    bool is_odd = test->pos % 2;

    if(test->type == SubGhzHalAsyncTxTestTypeNormal) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidStart) {
        if(test->pos == 0) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidMid) {
        if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeInvalidEnd) {
        if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL - 1) {
            test->pos++;
            return level_duration_make(!is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * 8) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetStart) {
        if(test->pos == 0) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetMid) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF / 2) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else if(test->type == SubGhzHalAsyncTxTestTypeResetEnd) {
        if(test->pos < FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL) {
            test->pos++;
            return level_duration_make(is_odd, SUBGHZ_HAL_TEST_DURATION);
        } else if(test->pos == FURI_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL) {
            test->pos++;
            return level_duration_reset();
        } else {
            furi_crash("Yield after reset");
        }
    } else {
        furi_crash();
    }
}

bool subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestType type) {
    SubGhzHalAsyncTxTest test = {0};
    test.type = type;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);
    furi_hal_subghz_set_frequency_and_path(433920000);

    if(!furi_hal_subghz_start_async_tx(subghz_hal_async_tx_test_yield, &test)) {
        mu_warn("SubGHZ transmission is prohibited");
        return false;
    }

    FuriHalCortexTimer timer = furi_hal_cortex_timer_get(30000000);

    while(!furi_hal_subghz_is_async_tx_complete()) {
        if(furi_hal_cortex_timer_is_expired(timer)) {
            furi_hal_subghz_stop_async_tx();
            furi_hal_subghz_sleep();
            return false;
        }
        furi_delay_ms(10);
    }
    furi_hal_subghz_stop_async_tx();
    furi_hal_subghz_sleep();

    return true;
}

MU_TEST(subghz_hal_async_tx_test) {
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeNormal),
        "Test furi_hal_async_tx normal");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidStart),
        "Test furi_hal_async_tx invalid start");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidMid),
        "Test furi_hal_async_tx invalid mid");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeInvalidEnd),
        "Test furi_hal_async_tx invalid end");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetStart),
        "Test furi_hal_async_tx reset start");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetMid),
        "Test furi_hal_async_tx reset mid");
    mu_assert(
        subghz_hal_async_tx_test_run(SubGhzHalAsyncTxTestTypeResetEnd),
        "Test furi_hal_async_tx reset end");
}

//test decoders
MU_TEST(subghz_decoder_came_atomo_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/came_atomo_raw.sub"), SUBGHZ_PROTOCOL_CAME_ATOMO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_ATOMO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_came_test) {
    mu_assert(
        subghz_decoder_test(EXT_PATH("unit_tests/subghz/came_raw.sub"), SUBGHZ_PROTOCOL_CAME_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_NAME " error\r\n");
}

MU_TEST(subghz_decoder_came_twee_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/came_twee_raw.sub"), SUBGHZ_PROTOCOL_CAME_TWEE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CAME_TWEE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_faac_slh_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/faac_slh_raw.sub"), SUBGHZ_PROTOCOL_FAAC_SLH_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_FAAC_SLH_NAME " error\r\n");
}

static void subghz_test_add_faac_learning_key(
    SubGhzKeystore* keystore,
    const char* name,
    uint64_t key) {
    SubGhzKey* entry = SubGhzKeyArray_push_raw(subghz_keystore_get_data(keystore));
    entry->name = furi_string_alloc_set(name);
    entry->key = key;
    entry->type = KEELOQ_LEARNING_FAAC;
}

static uint64_t subghz_test_faac_slh_frame(
    uint32_t seed,
    uint64_t manufacturer_key,
    uint32_t counter) {
    const uint32_t fixed = 0x12345678;
    uint8_t nibbles[8];
    for(size_t i = 0; i < COUNT_OF(nibbles); i++) {
        const uint8_t shift = (uint8_t)((COUNT_OF(nibbles) - i - 1) * 4);
        nibbles[i] = (uint8_t)((fixed >> shift) & 0xF);
    }

    const uint32_t plain = (uint32_t)nibbles[6] << 28 | (uint32_t)nibbles[7] << 24 |
                           (uint32_t)nibbles[5] << 20 | (counter & 0xFFFFF);
    const uint64_t learning_key =
        subghz_protocol_keeloq_common_faac_learning(seed, manufacturer_key);
    const uint32_t hop = subghz_protocol_keeloq_common_encrypt(plain, learning_key);
    return (uint64_t)fixed << 32 | hop;
}

static FlipperFormat* subghz_test_faac_slh_file(
    uint64_t frame,
    uint32_t seed,
    const char* manufacturer) {
    const uint32_t bit_count = 64;
    uint8_t key_data[sizeof(frame)] = {0};
    uint8_t seed_data[sizeof(seed)] = {0};

    for(size_t i = 0; i < sizeof(key_data); i++) {
        key_data[sizeof(key_data) - i - 1] = (uint8_t)(frame >> (i * 8));
    }
    for(size_t i = 0; i < sizeof(seed_data); i++) {
        seed_data[sizeof(seed_data) - i - 1] = (uint8_t)(seed >> (i * 8));
    }

    FlipperFormat* file = flipper_format_string_alloc();
    if(!flipper_format_write_uint32(file, "Bit", &bit_count, 1) ||
       !flipper_format_write_hex(file, "Key", key_data, sizeof(key_data)) ||
       !flipper_format_write_hex(file, "Seed", seed_data, sizeof(seed_data)) ||
       (manufacturer &&
        !flipper_format_write_string_cstr(file, "Manufacture", manufacturer))) {
        flipper_format_free(file);
        return NULL;
    }

    return file;
}

MU_TEST(subghz_decoder_faac_slh_manufacturer_metadata_test) {
    const uint64_t faac_test_key = 0x0123456789ABCDEF;
    const uint64_t genius_test_key = 0xFEDCBA9876543210;
    const uint32_t test_seed = 0x89ABCDEF;
    const uint32_t test_counter = 0x12344;

    SubGhzEnvironment* environment = subghz_environment_alloc();
    SubGhzKeystore* keystore = subghz_environment_get_keystore(environment);
    subghz_test_add_faac_learning_key(keystore, "FAAC_SLH", faac_test_key);
    subghz_test_add_faac_learning_key(keystore, "Genius", genius_test_key);

    void* decoder = subghz_protocol_faac_slh_decoder.alloc(environment);
    FuriString* output = furi_string_alloc();
    FlipperFormat* input = subghz_test_faac_slh_file(
        subghz_test_faac_slh_frame(test_seed, genius_test_key, test_counter),
        test_seed,
        "Genius");
    mu_check(input != NULL);
    mu_assert_int_eq(
        SubGhzProtocolStatusOk,
        subghz_protocol_faac_slh_decoder.deserialize(decoder, input));

    subghz_protocol_faac_slh_decoder.get_string(decoder, output);
    mu_check(furi_string_search_str(output, "Genius") != FURI_STRING_FAILURE);
    mu_check(furi_string_search_str(output, "Cnt:12344") != FURI_STRING_FAILURE);

    FlipperFormat* saved = flipper_format_string_alloc();
    FuriString* saved_manufacturer = furi_string_alloc();
    SubGhzRadioPreset preset = {
        .name = furi_string_alloc_set("AM650"),
        .frequency = 433920000,
    };
    mu_assert_int_eq(
        SubGhzProtocolStatusOk,
        subghz_protocol_faac_slh_decoder.serialize(decoder, saved, &preset));
    mu_check(flipper_format_rewind(saved));
    mu_check(flipper_format_read_string(saved, "Manufacture", saved_manufacturer));
    mu_assert_string_eq("Genius", furi_string_get_cstr(saved_manufacturer));

    FlipperFormat* legacy_input = subghz_test_faac_slh_file(
        subghz_test_faac_slh_frame(test_seed, faac_test_key, test_counter), test_seed, NULL);
    mu_check(legacy_input != NULL);
    mu_assert_int_eq(
        SubGhzProtocolStatusOk,
        subghz_protocol_faac_slh_decoder.deserialize(decoder, legacy_input));
    furi_string_reset(output);
    subghz_protocol_faac_slh_decoder.get_string(decoder, output);
    mu_check(furi_string_search_str(output, "Genius") == FURI_STRING_FAILURE);
    mu_check(furi_string_search_str(output, "Cnt:12344") != FURI_STRING_FAILURE);

    FlipperFormat* legacy_saved = flipper_format_string_alloc();
    mu_assert_int_eq(
        SubGhzProtocolStatusOk,
        subghz_protocol_faac_slh_decoder.serialize(decoder, legacy_saved, &preset));
    mu_check(flipper_format_rewind(legacy_saved));
    mu_check(!flipper_format_read_string(legacy_saved, "Manufacture", saved_manufacturer));

    furi_string_free(preset.name);
    furi_string_free(saved_manufacturer);
    flipper_format_free(legacy_saved);
    flipper_format_free(legacy_input);
    flipper_format_free(saved);
    furi_string_free(output);
    flipper_format_free(input);
    subghz_protocol_faac_slh_decoder.free(decoder);
    subghz_environment_free(environment);
}

MU_TEST(subghz_loading_label_api_export_test) {
    Loading* loading = loading_alloc();
    mu_check(loading != NULL);

    loading_set_text(loading, "Testing public loading label");
    loading_set_progress(loading, 0.5f);
    loading_reset_progress(loading);

    loading_free(loading);
}

MU_TEST(subghz_decoder_gate_tx_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/gate_tx_raw.sub"), SUBGHZ_PROTOCOL_GATE_TX_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_GATE_TX_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hormann_hsm_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hormann_hsm_raw.sub"), SUBGHZ_PROTOCOL_HORMANN_HSM_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HORMANN_HSM_NAME " error\r\n");
}

MU_TEST(subghz_decoder_ido_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/ido_117_111_raw.sub"), SUBGHZ_PROTOCOL_IDO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_IDO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_keeloq_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/doorhan_raw.sub"), SUBGHZ_PROTOCOL_KEELOQ_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_KEELOQ_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nero_radio_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nero_radio_raw.sub"), SUBGHZ_PROTOCOL_NERO_RADIO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NERO_RADIO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nero_sketch_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nero_sketch_raw.sub"), SUBGHZ_PROTOCOL_NERO_SKETCH_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NERO_SKETCH_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_flo_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_flo_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLO_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLO_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_flor_s_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_flor_s_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME " error\r\n");
}

MU_TEST(subghz_decoder_princeton_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/Princeton_raw.sub"), SUBGHZ_PROTOCOL_PRINCETON_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_PRINCETON_NAME " error\r\n");
}

MU_TEST(subghz_decoder_somfy_keytis_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/Somfy_keytis_raw.sub"), SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_somfy_telis_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/somfy_telis_raw.sub"), SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_linear_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/linear_raw.sub"), SUBGHZ_PROTOCOL_LINEAR_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LINEAR_NAME " error\r\n");
}

MU_TEST(subghz_decoder_linear_delta3_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/linear_delta3_raw.sub"),
            SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME " error\r\n");
}

MU_TEST(subghz_decoder_megacode_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/megacode_raw.sub"), SUBGHZ_PROTOCOL_MEGACODE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MEGACODE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_secplus_v1_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/security_pls_1_0_raw.sub"),
            SUBGHZ_PROTOCOL_SECPLUS_V1_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SECPLUS_V1_NAME " error\r\n");
}

MU_TEST(subghz_decoder_secplus_v2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/security_pls_2_0_raw.sub"),
            SUBGHZ_PROTOCOL_SECPLUS_V2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SECPLUS_V2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_holtek_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/holtek_raw.sub"), SUBGHZ_PROTOCOL_HOLTEK_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLTEK_NAME " error\r\n");
}

MU_TEST(subghz_decoder_power_smart_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/power_smart_raw.sub"), SUBGHZ_PROTOCOL_POWER_SMART_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_POWER_SMART_NAME " error\r\n");
}

MU_TEST(subghz_decoder_marantec_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/marantec_raw.sub"), SUBGHZ_PROTOCOL_MARANTEC_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MARANTEC_NAME " error\r\n");
}

MU_TEST(subghz_decoder_bett_test) {
    mu_assert(
        subghz_decoder_test(EXT_PATH("unit_tests/subghz/bett_raw.sub"), SUBGHZ_PROTOCOL_BETT_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_BETT_NAME " error\r\n");
}

MU_TEST(subghz_decoder_doitrand_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/doitrand_raw.sub"), SUBGHZ_PROTOCOL_DOITRAND_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DOITRAND_NAME " error\r\n");
}

MU_TEST(subghz_decoder_phoenix_v2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/phoenix_v2_raw.sub"), SUBGHZ_PROTOCOL_PHOENIX_V2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_PHOENIX_V2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_honeywell_wdb_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/honeywell_wdb_raw.sub"),
            SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME " error\r\n");
}

MU_TEST(subghz_decoder_magellan_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/magellan_raw.sub"), SUBGHZ_PROTOCOL_MAGELLAN_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MAGELLAN_NAME " error\r\n");
}

MU_TEST(subghz_decoder_intertechno_v3_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/intertechno_v3_raw.sub"),
            SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME " error\r\n");
}

MU_TEST(subghz_decoder_clemsa_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/clemsa_raw.sub"), SUBGHZ_PROTOCOL_CLEMSA_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_CLEMSA_NAME " error\r\n");
}

MU_TEST(subghz_decoder_ansonic_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/ansonic_raw.sub"), SUBGHZ_PROTOCOL_ANSONIC_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ANSONIC_NAME " error\r\n");
}

MU_TEST(subghz_decoder_smc5326_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/smc5326_raw.sub"), SUBGHZ_PROTOCOL_SMC5326_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_SMC5326_NAME " error\r\n");
}

MU_TEST(subghz_decoder_holtek_ht12x_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/holtek_ht12x_raw.sub"), SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME " error\r\n");
}

MU_TEST(subghz_decoder_dooya_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/dooya_raw.sub"), SUBGHZ_PROTOCOL_DOOYA_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DOOYA_NAME " error\r\n");
}

MU_TEST(subghz_decoder_alutech_at_4n_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/alutech_at_4n_raw.sub"),
            SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME " error\r\n");
}

MU_TEST(subghz_decoder_nice_one_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/nice_one_raw.sub"), SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME " error\r\n");
}

MU_TEST(subghz_decoder_kinggates_stylo4k_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/kinggates_stylo4k_raw.sub"),
            SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME " error\r\n");
}

MU_TEST(subghz_decoder_mastercode_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/mastercode_raw.sub"), SUBGHZ_PROTOCOL_MASTERCODE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MASTERCODE_NAME " error\r\n");
}

MU_TEST(subghz_decoder_dickert_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/dickert_raw.sub"), SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME " error\r\n");
}

MU_TEST(subghz_decoder_legrand_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/legrand_raw.sub"), SUBGHZ_PROTOCOL_LEGRAND_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_LEGRAND_NAME " error\r\n");
}

MU_TEST(subghz_decoder_marantec24_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/marantec24_raw.sub"), SUBGHZ_PROTOCOL_MARANTEC24_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_MARANTEC24_NAME " error\r\n");
}

MU_TEST(subghz_decoder_roger_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/roger_raw.sub"), SUBGHZ_PROTOCOL_ROGER_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_ROGER_NAME " error\r\n");
}

/* NOTE: subghz_decoder_test only asserts the decoder fires on the capture
 * (>=1 frame); it does NOT verify the decoded value. For Telcoma Edge that
 * means it would not catch a polarity-complemented decode (0x00CF603F instead
 * of the validated 0xFF309FC0). TODO: strengthen to assert the recovered key
 * value, and add an encoder round-trip test, once the harness supports it. */
MU_TEST(subghz_decoder_telcoma_edge_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/telcoma_edge_raw.sub"), SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME " error\r\n");
}

/* Non-gate channel capture (33-bit on-wire frame, one-hot channel marker) —
 * exercises the channel-aware decode path that the gate (32-bit) test misses. */
MU_TEST(subghz_decoder_telcoma_edge_ch_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/telcoma_edge_ch_raw.sub"),
            SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME " (channel) error\r\n");
}

MU_TEST(subghz_decoder_feron_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/feron_raw.sub"), SUBGHZ_PROTOCOL_FERON_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_FERON_NAME " error\r\n");
}

MU_TEST(subghz_decoder_gangqi_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/gangqi_raw.sub"), SUBGHZ_PROTOCOL_GANGQI_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_GANGQI_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hollarm_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hollarm_raw.sub"), SUBGHZ_PROTOCOL_HOLLARM_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HOLLARM_NAME " error\r\n");
}

MU_TEST(subghz_decoder_reversrb2_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/revers_rb2_raw.sub"), SUBGHZ_PROTOCOL_REVERSRB2_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_REVERSRB2_NAME " error\r\n");
}

MU_TEST(subghz_decoder_hay21_test) {
    mu_assert(
        subghz_decoder_test(
            EXT_PATH("unit_tests/subghz/hay21_raw.sub"), SUBGHZ_PROTOCOL_HAY21_NAME),
        "Test decoder " SUBGHZ_PROTOCOL_HAY21_NAME " error\r\n");
}

//test encoders
MU_TEST(subghz_encoder_princeton_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/princeton.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_PRINCETON_NAME " error\r\n");
}

MU_TEST(subghz_encoder_came_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/came.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CAME_NAME " error\r\n");
}

MU_TEST(subghz_encoder_came_twee_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/came_twee.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CAME_TWEE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_gate_tx_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/gate_tx.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_GATE_TX_NAME " error\r\n");
}

MU_TEST(subghz_encoder_nice_flo_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/nice_flo.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_NICE_FLO_NAME " error\r\n");
}

MU_TEST(subghz_encoder_keeloq_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/doorhan.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_KEELOQ_NAME " error\r\n");
}

MU_TEST(subghz_encoder_linear_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/linear.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LINEAR_NAME " error\r\n");
}

MU_TEST(subghz_encoder_linear_delta3_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/linear_delta3.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME " error\r\n");
}

MU_TEST(subghz_encoder_megacode_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/megacode.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MEGACODE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_holtek_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/holtek.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLTEK_NAME " error\r\n");
}

MU_TEST(subghz_encoder_secplus_v1_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/security_pls_1_0.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SECPLUS_V1_NAME " error\r\n");
}

MU_TEST(subghz_encoder_secplus_v2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/security_pls_2_0.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SECPLUS_V2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_power_smart_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/power_smart.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_POWER_SMART_NAME " error\r\n");
}

MU_TEST(subghz_encoder_marantec_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/marantec.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MARANTEC_NAME " error\r\n");
}

MU_TEST(subghz_encoder_bett_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/bett.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_BETT_NAME " error\r\n");
}

MU_TEST(subghz_encoder_doitrand_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/doitrand.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DOITRAND_NAME " error\r\n");
}

MU_TEST(subghz_encoder_phoenix_v2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/phoenix_v2.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_PHOENIX_V2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_honeywell_wdb_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/honeywell_wdb.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME " error\r\n");
}

MU_TEST(subghz_encoder_magellan_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/magellan.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MAGELLAN_NAME " error\r\n");
}

MU_TEST(subghz_encoder_intertechno_v3_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/intertechno_v3.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME " error\r\n");
}

MU_TEST(subghz_encoder_clemsa_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/clemsa.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_CLEMSA_NAME " error\r\n");
}

MU_TEST(subghz_encoder_ansonic_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/ansonic.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_ANSONIC_NAME " error\r\n");
}

MU_TEST(subghz_encoder_smc5326_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/smc5326.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_SMC5326_NAME " error\r\n");
}

MU_TEST(subghz_encoder_holtek_ht12x_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/holtek_ht12x.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME " error\r\n");
}

MU_TEST(subghz_encoder_dooya_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/dooya.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DOOYA_NAME " error\r\n");
}

MU_TEST(subghz_encoder_mastercode_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/mastercode.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MASTERCODE_NAME " error\r\n");
}

MU_TEST(subghz_encoder_dickert_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/dickert_mahs.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME " error\r\n");
}

MU_TEST(subghz_encoder_legrand_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/legrand.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_LEGRAND_NAME " error\r\n");
}

MU_TEST(subghz_encoder_feron_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/feron.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_FERON_NAME " error\r\n");
}

MU_TEST(subghz_encoder_gangqi_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/gangqi.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_GANGQI_NAME " error\r\n");
}

MU_TEST(subghz_encoder_hollarm_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/hollarm.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_HOLLARM_NAME " error\r\n");
}

MU_TEST(subghz_decoder_kia_v1_crc_metadata_test) {
    static const uint8_t key[] = {0x00, 0x12, 0x34, 0x56, 0x78, 0x01, 0x23, 0x6F};
    uint32_t bit_count = 57;
    FlipperFormat* flipper_format = flipper_format_string_alloc();
    void* decoder = subghz_protocol_decoder_kia_v1_alloc(environment_handler);
    FuriString* output = furi_string_alloc();

    mu_check(flipper_format_write_uint32(flipper_format, "Bit", &bit_count, 1));
    mu_check(flipper_format_write_hex(flipper_format, "Key", key, sizeof(key)));
    mu_assert_int_eq(
        SubGhzProtocolStatusOk,
        subghz_protocol_decoder_kia_v1_deserialize(decoder, flipper_format));

    subghz_protocol_decoder_kia_v1_get_string(decoder, output);
    mu_assert_int_not_eq(FURI_STRING_FAILURE, furi_string_search_str(output, "CRC:0F (OK)"));
    mu_assert_int_eq(FURI_STRING_FAILURE, furi_string_search_str(output, "CRC:6F"));

    furi_string_free(output);
    subghz_protocol_decoder_kia_v1_free(decoder);
    flipper_format_free(flipper_format);
}

MU_TEST(subghz_encoder_reversrb2_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/revers_rb2.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_REVERSRB2_NAME " error\r\n");
}

MU_TEST(subghz_encoder_roger_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/roger.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_ROGER_NAME " error\r\n");
}

MU_TEST(subghz_encoder_marantec24_test) {
    mu_assert(
        subghz_encoder_test(EXT_PATH("unit_tests/subghz/marantec24.sub")),
        "Test encoder " SUBGHZ_PROTOCOL_MARANTEC24_NAME " error\r\n");
}

MU_TEST(subghz_random_test) {
    mu_assert(subghz_decode_random_test(TEST_RANDOM_DIR_NAME), "Random test error\r\n");
}

MU_TEST_SUITE(subghz) {
    subghz_test_init();
    MU_RUN_TEST(subghz_keystore_test);

    MU_RUN_TEST(subghz_hal_async_tx_test);

    MU_RUN_TEST(subghz_decoder_came_atomo_test);
    MU_RUN_TEST(subghz_decoder_came_test);
    MU_RUN_TEST(subghz_decoder_came_twee_test);
    MU_RUN_TEST(subghz_decoder_faac_slh_test);
    MU_RUN_TEST(subghz_decoder_faac_slh_manufacturer_metadata_test);
    MU_RUN_TEST(subghz_loading_label_api_export_test);
    MU_RUN_TEST(subghz_decoder_gate_tx_test);
    MU_RUN_TEST(subghz_decoder_hormann_hsm_test);
    MU_RUN_TEST(subghz_decoder_ido_test);
    MU_RUN_TEST(subghz_decoder_keeloq_test);
    MU_RUN_TEST(subghz_decoder_nero_radio_test);
    MU_RUN_TEST(subghz_decoder_nero_sketch_test);
    MU_RUN_TEST(subghz_decoder_nice_flo_test);
    MU_RUN_TEST(subghz_decoder_nice_flor_s_test);
    MU_RUN_TEST(subghz_decoder_princeton_test);
    MU_RUN_TEST(subghz_decoder_somfy_keytis_test);
    MU_RUN_TEST(subghz_decoder_somfy_telis_test);
    MU_RUN_TEST(subghz_decoder_linear_test);
    MU_RUN_TEST(subghz_decoder_linear_delta3_test);
    MU_RUN_TEST(subghz_decoder_megacode_test);
    MU_RUN_TEST(subghz_decoder_secplus_v1_test);
    MU_RUN_TEST(subghz_decoder_secplus_v2_test);
    MU_RUN_TEST(subghz_decoder_holtek_test);
    MU_RUN_TEST(subghz_decoder_power_smart_test);
    MU_RUN_TEST(subghz_decoder_marantec_test);
    MU_RUN_TEST(subghz_decoder_bett_test);
    MU_RUN_TEST(subghz_decoder_doitrand_test);
    MU_RUN_TEST(subghz_decoder_phoenix_v2_test);
    MU_RUN_TEST(subghz_decoder_honeywell_wdb_test);
    MU_RUN_TEST(subghz_decoder_magellan_test);
    MU_RUN_TEST(subghz_decoder_intertechno_v3_test);
    MU_RUN_TEST(subghz_decoder_clemsa_test);
    MU_RUN_TEST(subghz_decoder_ansonic_test);
    MU_RUN_TEST(subghz_decoder_smc5326_test);
    MU_RUN_TEST(subghz_decoder_holtek_ht12x_test);
    MU_RUN_TEST(subghz_decoder_dooya_test);
    MU_RUN_TEST(subghz_decoder_alutech_at_4n_test);
    MU_RUN_TEST(subghz_decoder_nice_one_test);
    MU_RUN_TEST(subghz_decoder_kinggates_stylo4k_test);
    MU_RUN_TEST(subghz_decoder_mastercode_test);
    MU_RUN_TEST(subghz_decoder_dickert_test);
    MU_RUN_TEST(subghz_decoder_roger_test);
    MU_RUN_TEST(subghz_decoder_telcoma_edge_test);
    MU_RUN_TEST(subghz_decoder_telcoma_edge_ch_test);
    MU_RUN_TEST(subghz_decoder_hollarm_test);
    MU_RUN_TEST(subghz_decoder_reversrb2_test);
    MU_RUN_TEST(subghz_decoder_gangqi_test);
    MU_RUN_TEST(subghz_decoder_hay21_test);
    MU_RUN_TEST(subghz_decoder_feron_test);
    MU_RUN_TEST(subghz_decoder_legrand_test);
    MU_RUN_TEST(subghz_decoder_marantec24_test);

    MU_RUN_TEST(subghz_encoder_princeton_test);
    MU_RUN_TEST(subghz_encoder_came_test);
    MU_RUN_TEST(subghz_encoder_came_twee_test);
    MU_RUN_TEST(subghz_encoder_gate_tx_test);
    MU_RUN_TEST(subghz_encoder_nice_flo_test);
    MU_RUN_TEST(subghz_encoder_keeloq_test);
    MU_RUN_TEST(subghz_encoder_linear_test);
    MU_RUN_TEST(subghz_encoder_linear_delta3_test);
    MU_RUN_TEST(subghz_encoder_megacode_test);
    MU_RUN_TEST(subghz_encoder_holtek_test);
    MU_RUN_TEST(subghz_encoder_secplus_v1_test);
    MU_RUN_TEST(subghz_encoder_secplus_v2_test);
    MU_RUN_TEST(subghz_encoder_power_smart_test);
    MU_RUN_TEST(subghz_encoder_marantec_test);
    MU_RUN_TEST(subghz_encoder_bett_test);
    MU_RUN_TEST(subghz_encoder_doitrand_test);
    MU_RUN_TEST(subghz_encoder_phoenix_v2_test);
    MU_RUN_TEST(subghz_encoder_honeywell_wdb_test);
    MU_RUN_TEST(subghz_encoder_magellan_test);
    MU_RUN_TEST(subghz_encoder_intertechno_v3_test);
    MU_RUN_TEST(subghz_encoder_clemsa_test);
    MU_RUN_TEST(subghz_encoder_ansonic_test);
    MU_RUN_TEST(subghz_encoder_smc5326_test);
    MU_RUN_TEST(subghz_encoder_holtek_ht12x_test);
    MU_RUN_TEST(subghz_encoder_dooya_test);
    MU_RUN_TEST(subghz_encoder_mastercode_test);
    MU_RUN_TEST(subghz_encoder_dickert_test);
    MU_RUN_TEST(subghz_encoder_feron_test);
    MU_RUN_TEST(subghz_encoder_roger_test);
    MU_RUN_TEST(subghz_encoder_gangqi_test);
    MU_RUN_TEST(subghz_encoder_marantec24_test);
    MU_RUN_TEST(subghz_encoder_hollarm_test);
    MU_RUN_TEST(subghz_encoder_reversrb2_test);
    MU_RUN_TEST(subghz_encoder_legrand_test);

    MU_RUN_TEST(subghz_decoder_kia_v1_crc_metadata_test);

    MU_RUN_TEST(subghz_random_test);
    subghz_test_deinit();
}

int run_minunit_test_subghz(void) {
    MU_RUN_SUITE(subghz);
    return MU_EXIT_CODE;
}

TEST_API_DEFINE(run_minunit_test_subghz)
