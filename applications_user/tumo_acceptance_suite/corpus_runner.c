#include "corpus_runner.h"
#include "corpus_raw.h"
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/toolbox/stream/stream.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <mbedtls/sha256.h>
#include <furi_hal_version.h>
#include <furi_hal_info.h>
#include <stdio.h>

#define CORPUS_DIR         EXT_PATH("apps_data/tumo_acceptance_suite/references")
#define CORPUS_TYPE        "Tumoflip Decoder Reference"
#define CORPUS_LIMIT       32U
#define CORPUS_FRAME_LIMIT 10000U

typedef struct {
    uint32_t pack;
    uint32_t frames;
    uint8_t input[32];
    uint8_t output[32];
    char path[256];
} CorpusReference;

struct CorpusRunner {
    Storage* storage;
    File* input;
    SubGhzEnvironment* environment;
    SubGhzProtocolPackRegistry* registry;
    SubGhzReceiver* receiver;
    FlipperFormat* decoded;
    SubGhzRadioPreset preset;
    CorpusRaw parser;
    mbedtls_sha256_context input_hash;
    mbedtls_sha256_context output_hash;
    CorpusReference expected;
    CorpusReference actual;
    FuriString* text;
    FuriString* details;
    uint32_t started;
    uint32_t size;
    uint32_t slot;
    uint32_t checked;
    uint32_t passed;
    uint32_t failed;
    bool batch;
    bool running;
    bool candidate;
    bool decode_error;
    char last_label[64];
    uint8_t labels;
};

static void corpus_close(CorpusRunner* r) {
    if(r->receiver) subghz_receiver_free(r->receiver);
    if(r->environment) subghz_environment_free(r->environment);
    if(r->registry) subghz_protocol_pack_registry_free(r->registry);
    r->receiver = NULL;
    r->environment = NULL;
    r->registry = NULL;
    storage_file_close(r->input);
}

static void
    corpus_decoded(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder, void* context) {
    UNUSED(receiver);
    CorpusRunner* r = context;
    if(r->decode_error) return;
    if(r->actual.frames >= CORPUS_FRAME_LIMIT) {
        r->decode_error = true;
        return;
    }
    stream_clean(flipper_format_get_raw_stream(r->decoded));
    r->preset.frequency = r->parser.frequency;
    if(subghz_protocol_decoder_base_serialize(decoder, r->decoded, &r->preset) !=
       SubGhzProtocolStatusOk) {
        r->decode_error = true;
        return;
    }
    Stream* stream = flipper_format_get_raw_stream(r->decoded);
    const size_t size = stream_size(stream);
    if(!size || size > 8192 || !stream_rewind(stream)) {
        r->decode_error = true;
        return;
    }
    // Length-delimited canonical serialization, not the GUI description or the 8-bit history hash.
    uint8_t length[4] = {size & 255, (size >> 8) & 255, (size >> 16) & 255, (size >> 24) & 255};
    r->decode_error = mbedtls_sha256_update(&r->output_hash, length, sizeof(length)) != 0;
    uint8_t buffer[128];
    for(size_t remaining = size; remaining && !r->decode_error;) {
        size_t n = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if(stream_read(stream, buffer, n) != n ||
           mbedtls_sha256_update(&r->output_hash, buffer, n))
            r->decode_error = true;
        remaining -= n;
    }
    r->actual.frames++;
    if(r->labels < 3 && strcmp(r->last_label, decoder->protocol->name)) {
        strlcpy(r->last_label, decoder->protocol->name, sizeof(r->last_label));
        furi_string_cat_printf(r->details, "%s\n", r->last_label);
        r->labels++;
    }
}

static bool corpus_emit(bool level, uint32_t duration, void* context) {
    CorpusRunner* r = context;
    subghz_receiver_decode(r->receiver, level, duration);
    return !r->decode_error;
}

static const char* corpus_open(CorpusRunner* r, const char* path, uint32_t pack) {
    corpus_close(r);
    r->decode_error = r->candidate = false;
    r->last_label[0] = 0;
    r->labels = 0;
    memset(&r->actual, 0, sizeof(r->actual));
    furi_string_reset(r->details);
    if(pack >= SubGhzProtocolPackGroupCount || strncmp(path, "/ext/", 5) ||
       strlcpy(r->actual.path, path, sizeof(r->actual.path)) >= sizeof(r->actual.path))
        return "Invalid reference path/pack";
    for(const char* c = path; *c; c++)
        if((unsigned char)*c < 32) return "Invalid reference path";
    r->actual.pack = pack;
    if(!storage_file_open(r->input, path, FSAM_READ, FSOM_OPEN_EXISTING))
        return "Cannot read RAW file";
    uint64_t size = storage_file_size(r->input);
    if(!size || size > CORPUS_RAW_MAX_BYTES) return "RAW size limit (4 MiB)";
    r->size = size;
    r->registry = subghz_protocol_pack_registry_alloc(
        &subghz_protocol_registry, EXT_PATH("apps_data/subghz/plugins"), pack);
    const SubGhzProtocolPackReport* report = subghz_protocol_pack_registry_get_report(r->registry);
    if(!report || report->loaded_plugin_count != report->expected_plugin_count)
        return "Protocol Pack incomplete";
    r->environment = subghz_environment_alloc();
    if(!subghz_environment_load_keystore(r->environment, SUBGHZ_KEYSTORE_DIR_NAME))
        return "System keystore unavailable";
    subghz_environment_load_keystore(r->environment, SUBGHZ_KEYSTORE_DIR_EXTENDED);
    subghz_environment_load_keystore(r->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        r->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        r->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_protocol_registry(
        r->environment, subghz_protocol_pack_registry_get(r->registry));
    r->receiver = subghz_receiver_alloc_init(r->environment);
    subghz_receiver_set_filter(r->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(r->receiver, corpus_decoded, r);
    subghz_receiver_reset(r->receiver);
    corpus_raw_init(&r->parser, corpus_emit, r);
    if(mbedtls_sha256_starts(&r->input_hash, 0) || mbedtls_sha256_starts(&r->output_hash, 0))
        return "Hash initialization failed";
    r->started = furi_get_tick();
    return NULL;
}

static bool corpus_read_reference(CorpusRunner* r, const char* path) {
    FileInfo info;
    if(storage_common_stat(r->storage, path, &info) != FSE_OK || info.size > 2048) return false;
    FlipperFormat* file = flipper_format_file_alloc(r->storage);
    FuriString* value = furi_string_alloc();
    uint32_t version;
    memset(&r->expected, 0, sizeof(r->expected));
    flipper_format_set_strict_mode(file, true);
    bool ok = flipper_format_file_open_existing(file, path) &&
              flipper_format_read_header(file, value, &version) && version == 1 &&
              !strcmp(furi_string_get_cstr(value), CORPUS_TYPE) &&
              flipper_format_read_string(file, "Source", value) &&
              strlcpy(r->expected.path, furi_string_get_cstr(value), sizeof(r->expected.path)) <
                  sizeof(r->expected.path) &&
              flipper_format_read_uint32(file, "Pack", &r->expected.pack, 1) &&
              flipper_format_read_uint32(file, "Frames", &r->expected.frames, 1) &&
              r->expected.frames && r->expected.frames <= CORPUS_FRAME_LIMIT &&
              flipper_format_read_hex(file, "InputSHA256", r->expected.input, 32) &&
              flipper_format_read_hex(file, "OutputSHA256", r->expected.output, 32);
    flipper_format_free(file);
    furi_string_free(value);
    return ok;
}

static bool corpus_next(CorpusRunner* r) {
    char path[128];
    while(r->slot < CORPUS_LIMIT) {
        unsigned slot = r->slot++;
        snprintf(path, sizeof(path), CORPUS_DIR "/reference_%02u.tref", slot);
        FS_Error stat = storage_common_stat(r->storage, path, NULL);
        if(stat == FSE_NOT_EXIST) continue;
        r->checked++;
        const char* error = stat != FSE_OK ? "Cannot read reference" :
                            !corpus_read_reference(r, path) ?
                                             "Invalid reference" :
                                             corpus_open(r, r->expected.path, r->expected.pack);
        if(!error) return true;
        corpus_close(r);
        r->failed++;
        furi_string_cat_printf(r->text, "[%02u] ERROR: %s\n", slot, error);
    }
    r->running = false;
    FuriString* details = furi_string_alloc_set(r->text);
    furi_string_printf(
        r->text,
        "%lu/%lu matched; %lu failed\n\n%s\n%s",
        r->passed,
        r->checked,
        r->failed,
        furi_string_get_cstr(details),
        r->checked ? "Offline only.\nRF hardware not tested." :
                     "No references.\nAdd a RAW file first.");
    furi_string_free(details);
    return false;
}

CorpusRunner* corpus_runner_alloc(Storage* storage) {
    CorpusRunner* r = calloc(1, sizeof(*r));
    r->storage = storage;
    r->input = storage_file_alloc(storage);
    r->text = furi_string_alloc();
    r->details = furi_string_alloc();
    r->decoded = flipper_format_string_alloc();
    // Neutral serialization metadata; no hardware preset is configured or transmitted.
    r->preset.name = furi_string_alloc_set("AM650");
    mbedtls_sha256_init(&r->input_hash);
    mbedtls_sha256_init(&r->output_hash);
    return r;
}

void corpus_runner_free(CorpusRunner* r) {
    corpus_close(r);
    storage_file_free(r->input);
    flipper_format_free(r->decoded);
    furi_string_free(r->preset.name);
    furi_string_free(r->text);
    furi_string_free(r->details);
    mbedtls_sha256_free(&r->input_hash);
    mbedtls_sha256_free(&r->output_hash);
    free(r);
}

bool corpus_runner_add(CorpusRunner* r, const char* path, SubGhzProtocolPackGroup pack) {
    r->batch = false;
    const char* error = corpus_open(r, path, pack);
    furi_string_set(r->text, error ? error : "Decoding RAW file...\nBack: cancel");
    r->running = !error;
    if(error) corpus_close(r);
    return r->running;
}

bool corpus_runner_check(CorpusRunner* r) {
    corpus_close(r);
    r->candidate = false;
    r->batch = r->running = true;
    r->slot = r->checked = r->passed = r->failed = 0;
    furi_string_printf(
        r->text,
        "Decoder checks / schema 1\nFirmware: %s\n\n",
        version_get_version(furi_hal_version_get_firmware_version()));
    return corpus_next(r);
}

bool corpus_runner_step(CorpusRunner* r) {
    if(!r->running) return false;
    uint8_t buffer[512];
    size_t n = storage_file_read(r->input, buffer, sizeof(buffer));
    const char* error = NULL;
    bool done = false;
    if(storage_file_get_error(r->input) != FSE_OK)
        error = "RAW read error";
    else if(furi_get_tick() - r->started > furi_ms_to_ticks(120000))
        error = "Decode timeout";
    else if(n) {
        if(mbedtls_sha256_update(&r->input_hash, buffer, n) ||
           !corpus_raw_feed(&r->parser, (const char*)buffer, n))
            error = "Invalid RAW / decode error";
    } else {
        done = true;
        if(r->parser.bytes != r->size || !corpus_raw_finish(&r->parser))
            error = "Truncated / invalid RAW";
        else if(!r->actual.frames)
            error = "No decoded frames";
        else if(
            mbedtls_sha256_finish(&r->input_hash, r->actual.input) ||
            mbedtls_sha256_finish(&r->output_hash, r->actual.output))
            error = "Hash failed";
        else if(r->batch && memcmp(r->expected.input, r->actual.input, 32))
            error = "Input changed";
        else if(
            r->batch && (r->expected.frames != r->actual.frames ||
                         memcmp(r->expected.output, r->actual.output, 32)))
            error = "Decoder result changed";
    }
    if(!error && !done) return true;
    corpus_close(r);
    if(r->batch) {
        if(error)
            r->failed++;
        else
            r->passed++;
        furi_string_cat_printf(
            r->text,
            "[%02lu] %s\nFrames: %lu\n\n",
            r->slot - 1,
            error ? error : "MATCH",
            r->actual.frames);
        return corpus_next(r);
    }
    r->running = false;
    r->candidate = !error;
    if(error)
        furi_string_set(r->text, error);
    else
        furi_string_printf(
            r->text,
            "Frames: %lu\nProtocols:\n%s\nBack > Save as reference\nif this is expected.\nSource file unchanged.",
            r->actual.frames,
            furi_string_get_cstr(r->details));
    return false;
}

void corpus_runner_cancel(CorpusRunner* r) {
    corpus_close(r);
    r->running = r->candidate = false;
    furi_string_set(r->text, "Cancelled\n\nIncomplete run.\nNo reference saved.");
}

bool corpus_runner_can_save(const CorpusRunner* r) {
    return r->candidate && !r->running;
}

static void corpus_hex(FuriString* text, const char* key, const uint8_t* digest) {
    furi_string_cat_printf(text, "%s:", key);
    for(unsigned i = 0; i < 32; i++)
        furi_string_cat_printf(text, " %02X", digest[i]);
    furi_string_cat(text, "\n");
}

bool corpus_runner_save(CorpusRunner* r) {
    if(!corpus_runner_can_save(r)) return false;
    storage_common_mkdir(r->storage, EXT_PATH("apps_data/tumo_acceptance_suite"));
    FS_Error mkdir = storage_common_mkdir(r->storage, CORPUS_DIR);
    if(mkdir != FSE_OK && mkdir != FSE_EXIST) return false;
    FuriString* text = furi_string_alloc_printf(
        "Filetype: " CORPUS_TYPE "\nVersion: 1\nSource: %s\nPack: %lu\nFrames: %lu\n",
        r->actual.path,
        r->actual.pack,
        r->actual.frames);
    corpus_hex(text, "InputSHA256", r->actual.input);
    corpus_hex(text, "OutputSHA256", r->actual.output);
    uint16_t major, minor;
    furi_hal_info_get_api_version(&major, &minor);
    furi_string_cat_printf(
        text,
        "Firmware: %s\nAPI: %u.%u\n",
        version_get_version(furi_hal_version_get_firmware_version()),
        major,
        minor);
    File* file = storage_file_alloc(r->storage);
    bool saved = false;
    char path[128];
    for(unsigned i = 0; i < CORPUS_LIMIT; i++) {
        snprintf(path, sizeof(path), CORPUS_DIR "/reference_%02u.tref", i);
        if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW)) {
            saved = storage_file_write(file, furi_string_get_cstr(text), furi_string_size(text)) ==
                        furi_string_size(text) &&
                    storage_file_sync(file);
            saved = storage_file_close(file) && saved;
            if(!saved) storage_common_remove(r->storage, path);
            break;
        }
        FS_Error error = storage_file_get_error(file);
        storage_file_close(file);
        if(error != FSE_EXIST) break;
    }
    storage_file_free(file);
    furi_string_free(text);
    if(saved) {
        r->candidate = false;
        const char* name = strrchr(path, '/');
        furi_string_printf(
            r->text,
            "Reference saved:\n%s\n\nKeep the original RAW file.\nNo reference replaced.\n\nFolder:\napps_data/\ntumo_acceptance_suite/\nreferences/",
            name ? name + 1 : path);
    }
    return saved;
}

const char* corpus_runner_text(CorpusRunner* r) {
    return furi_string_get_cstr(r->text);
}
uint8_t corpus_runner_progress(CorpusRunner* r) {
    return r->running && r->size ? (uint8_t)((uint64_t)r->parser.bytes * 100 / r->size) : 0;
}

bool corpus_runner_export(CorpusRunner* r) {
    if(r->running || furi_string_empty(r->text)) return false;
    char path[144];
    storage_common_mkdir(r->storage, EXT_PATH("apps_data/tumo_acceptance_suite"));
    snprintf(
        path,
        sizeof(path),
        EXT_PATH("apps_data/tumo_acceptance_suite/decoder_%lu.txt"),
        furi_get_tick());
    File* file = storage_file_alloc(r->storage);
    bool created = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    bool ok = created &&
              storage_file_write(file, furi_string_get_cstr(r->text), furi_string_size(r->text)) ==
                  furi_string_size(r->text) &&
              storage_file_sync(file);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(created && !ok) storage_common_remove(r->storage, path);
    return ok;
}
