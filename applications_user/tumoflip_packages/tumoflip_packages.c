#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>

#include <string.h>

#define TUMO_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMO_INSTALL_STATE_PATH EXT_PATH(".tumoflip/install-state.json")
#define TUMO_SHA256_HEX_LEN     64
#define TUMO_MAX_AUDIT_FILES    256
#define TUMO_MAX_LINE_LEN       256

typedef enum {
    TumoPackagesViewMain,
    TumoPackagesViewText,
} TumoPackagesView;

typedef enum {
    TumoPackagesMenuAudit,
    TumoPackagesMenuState,
    TumoPackagesMenuAbout,
} TumoPackagesMenu;

typedef enum {
    TumoPackagesEventAudit,
    TumoPackagesEventState,
    TumoPackagesEventAbout,
} TumoPackagesEvent;

typedef struct {
    const char* legacy;
    const char* canonical;
} TumoLegacyPath;

static const TumoLegacyPath tumo_legacy_paths[] = {
    {"/ext/apps/ARF Tools/ARF Car Emulate.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_car_emulate.fap"},
    {"/ext/apps/ARF Tools/ARF Counter BF.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_counter_bf.fap"},
    {"/ext/apps/ARF Tools/ARF Frequency Analyzer.fap",
     "/ext/apps/ARF Tools/arf_frequency_analyzer.fap"},
    {"/ext/apps/ARF Tools/ARF KeeLoq.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_keeloq.fap"},
    {"/ext/apps/ARF Tools/ARF PSA Decrypt.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_psa_decrypt.fap"},
    {"/ext/apps/ARF Tools/ARF Status.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_status.fap"},
    {"/ext/apps/ARF Tools/ARF Sub-GHz Full.fap",
     "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/ARF Tools/ARF Sub-GHz.fap", "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/ARF Tools/ProtoPirate.fap",
     "/ext/apps_data/arf_subghz_full/modules/proto_pirate.fap"},
    {"/ext/apps/ARF Tools/Sub-GHz Bruteforcer.fap",
     "/ext/apps_data/arf_subghz_full/modules/subghz_bruteforcer.fap"},
    {"/ext/apps/ARF Tools/arf_car_emulate.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_car_emulate.fap"},
    {"/ext/apps/ARF Tools/arf_counter_bf.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_counter_bf.fap"},
    {"/ext/apps/ARF Tools/arf_keeloq.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_keeloq.fap"},
    {"/ext/apps/ARF Tools/arf_psa_decrypt.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_psa_decrypt.fap"},
    {"/ext/apps/ARF Tools/arf_status.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_status.fap"},
    {"/ext/apps/ARF Tools/arf_subghz.fap", "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/ARF Tools/arf_subghz_standard.fap",
     "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/ARF Tools/proto_pirate.fap",
     "/ext/apps_data/arf_subghz_full/modules/proto_pirate.fap"},
    {"/ext/apps/ARF Tools/rolljam.fap",
     "/ext/apps_data/arf_subghz_full/modules/rolljam.fap"},
    {"/ext/apps/ARF Tools/rolljam_standalone.fap",
     "/ext/apps_data/arf_subghz_full/modules/rolljam.fap"},
    {"/ext/apps/ARF Tools/subghz_bruteforcer.fap",
     "/ext/apps_data/arf_subghz_full/modules/subghz_bruteforcer.fap"},
    {"/ext/apps_data/arf_subghz_full/modules/arf_frequency_analyzer.fap",
     "/ext/apps/ARF Tools/arf_frequency_analyzer.fap"},
    {"/ext/apps_data/arf_subghz_full/modules/arf_subghz_standard.fap",
     "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/Sub-GHz/arf_car_emulate.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_car_emulate.fap"},
    {"/ext/apps/Sub-GHz/arf_counter_bf.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_counter_bf.fap"},
    {"/ext/apps/Sub-GHz/arf_frequency_analyzer.fap",
     "/ext/apps/ARF Tools/arf_frequency_analyzer.fap"},
    {"/ext/apps/Sub-GHz/arf_keeloq.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_keeloq.fap"},
    {"/ext/apps/Sub-GHz/arf_psa_decrypt.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_psa_decrypt.fap"},
    {"/ext/apps/Sub-GHz/arf_status.fap",
     "/ext/apps_data/arf_subghz_full/modules/arf_status.fap"},
    {"/ext/apps/Sub-GHz/arf_subghz_full.fap", "/ext/apps/ARF Tools/arf_subghz_full.fap"},
    {"/ext/apps/Sub-GHz/ble_killer.fap", "/ext/apps/ARF Tools/ble_killer.fap"},
    {"/ext/apps/Sub-GHz/garage_door_remote.fap",
     "/ext/apps/ARF Tools/garage_door_remote.fap"},
    {"/ext/apps/Sub-GHz/keeloq_keystore_decryptor.fap",
     "/ext/apps/ARF Tools/keeloq_keystore_decryptor.fap"},
    {"/ext/apps/Sub-GHz/proto_pirate.fap",
     "/ext/apps_data/arf_subghz_full/modules/proto_pirate.fap"},
    {"/ext/apps/Sub-GHz/rolljam.fap",
     "/ext/apps_data/arf_subghz_full/modules/rolljam.fap"},
    {"/ext/apps/Sub-GHz/rolljam_standalone.fap",
     "/ext/apps_data/arf_subghz_full/modules/rolljam.fap"},
    {"/ext/apps/Sub-GHz/subghz_bruteforcer.fap",
     "/ext/apps_data/arf_subghz_full/modules/subghz_bruteforcer.fap"},
    {"/ext/apps/Sub-GHz/subghz_raw_edit.fap", "/ext/apps/ARF Tools/subghz_raw_edit.fap"},
};

typedef struct {
    bool package_state_present;
    bool install_state_present;
    bool line_too_long;
    bool capped;
    uint16_t files_expected;
    uint16_t files_checked;
    uint16_t hash_ok;
    uint16_t missing;
    uint16_t mismatched;
    uint16_t unreadable;
    uint16_t invalid_paths;
    uint16_t parse_errors;
    uint16_t legacy_duplicates;
    uint16_t legacy_review;
} TumoAuditStats;

typedef struct {
    bool present;
    bool complete;
    uint16_t parse_errors;
    FuriString* release_id;
    FuriString* transaction;
    FuriString* firmware;
    FuriString* package_release;
    FuriString* groups;
    FuriString* installed_files;
    FuriString* cleanup_candidates;
    FuriString* rollback;
} TumoPackageState;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    TextBox* text_box;
    FuriString* text;
} TumoPackagesApp;

typedef struct {
    uint8_t data[64];
    uint32_t data_len;
    uint64_t bit_len;
    uint32_t state[8];
} TumoSha256;

static const uint32_t tumo_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

static uint32_t tumo_sha256_rotr(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void tumo_sha256_transform(TumoSha256* sha, const uint8_t data[64]) {
    uint32_t words[64];
    for(size_t i = 0; i < 16; i++) {
        words[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
                   ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
    }

    for(size_t i = 16; i < 64; i++) {
        const uint32_t s0 = tumo_sha256_rotr(words[i - 15], 7) ^
                            tumo_sha256_rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const uint32_t s1 = tumo_sha256_rotr(words[i - 2], 17) ^
                            tumo_sha256_rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    uint32_t f = sha->state[5];
    uint32_t g = sha->state[6];
    uint32_t h = sha->state[7];

    for(size_t i = 0; i < 64; i++) {
        const uint32_t s1 = tumo_sha256_rotr(e, 6) ^ tumo_sha256_rotr(e, 11) ^
                            tumo_sha256_rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + ch + tumo_sha256_k[i] + words[i];
        const uint32_t s0 = tumo_sha256_rotr(a, 2) ^ tumo_sha256_rotr(a, 13) ^
                            tumo_sha256_rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
    sha->state[5] += f;
    sha->state[6] += g;
    sha->state[7] += h;
}

static void tumo_sha256_init(TumoSha256* sha) {
    sha->data_len = 0;
    sha->bit_len = 0;
    sha->state[0] = 0x6a09e667;
    sha->state[1] = 0xbb67ae85;
    sha->state[2] = 0x3c6ef372;
    sha->state[3] = 0xa54ff53a;
    sha->state[4] = 0x510e527f;
    sha->state[5] = 0x9b05688c;
    sha->state[6] = 0x1f83d9ab;
    sha->state[7] = 0x5be0cd19;
}

static void tumo_sha256_update(TumoSha256* sha, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        sha->data[sha->data_len++] = data[i];
        if(sha->data_len == sizeof(sha->data)) {
            tumo_sha256_transform(sha, sha->data);
            sha->bit_len += 512;
            sha->data_len = 0;
        }
    }
}

static void tumo_sha256_final(TumoSha256* sha, uint8_t hash[32]) {
    size_t i = sha->data_len;

    sha->data[i++] = 0x80;
    if(i > 56) {
        while(i < 64) {
            sha->data[i++] = 0;
        }
        tumo_sha256_transform(sha, sha->data);
        i = 0;
    }

    while(i < 56) {
        sha->data[i++] = 0;
    }

    sha->bit_len += sha->data_len * 8;
    for(size_t byte = 0; byte < 8; byte++) {
        sha->data[63 - byte] = (sha->bit_len >> (byte * 8)) & 0xff;
    }
    tumo_sha256_transform(sha, sha->data);

    for(i = 0; i < 4; i++) {
        for(size_t word = 0; word < 8; word++) {
            hash[word * 4 + i] = (sha->state[word] >> (24 - i * 8)) & 0xff;
        }
    }
}

static void tumo_state_init(TumoPackageState* state) {
    state->present = false;
    state->complete = false;
    state->parse_errors = 0;
    state->release_id = furi_string_alloc();
    state->transaction = furi_string_alloc();
    state->firmware = furi_string_alloc();
    state->package_release = furi_string_alloc();
    state->groups = furi_string_alloc();
    state->installed_files = furi_string_alloc();
    state->cleanup_candidates = furi_string_alloc();
    state->rollback = furi_string_alloc();
}

static void tumo_state_free(TumoPackageState* state) {
    furi_string_free(state->release_id);
    furi_string_free(state->transaction);
    furi_string_free(state->firmware);
    furi_string_free(state->package_release);
    furi_string_free(state->groups);
    furi_string_free(state->installed_files);
    furi_string_free(state->cleanup_candidates);
    furi_string_free(state->rollback);
}

static void tumo_packages_set_text(TumoPackagesApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoPackagesViewText);
}

static bool tumo_path_is_file(Storage* storage, const char* path) {
    FileInfo file_info = {0};
    return (storage_common_stat(storage, path, &file_info) == FSE_OK) &&
           !file_info_is_dir(&file_info);
}

static bool tumo_path_is_safe_ext_file(const char* path) {
    if(strncmp(path, "/ext/", 5) != 0) {
        return false;
    }

    if(strstr(path, "/../") || strstr(path, "/./")) {
        return false;
    }

    size_t path_len = strlen(path);
    return !((path_len >= 3) && (strcmp(path + path_len - 3, "/..") == 0)) &&
           !((path_len >= 2) && (strcmp(path + path_len - 2, "/.") == 0));
}

static bool tumo_is_hex_char(char c) {
    return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
           ((c >= 'A') && (c <= 'F'));
}

static bool tumo_sha_is_valid(const char* sha) {
    if(strlen(sha) != TUMO_SHA256_HEX_LEN) {
        return false;
    }

    for(size_t i = 0; i < TUMO_SHA256_HEX_LEN; i++) {
        if(!tumo_is_hex_char(sha[i])) {
            return false;
        }
    }

    return true;
}

static char tumo_ascii_lower(char c) {
    if((c >= 'A') && (c <= 'F')) {
        return c + ('a' - 'A');
    }
    return c;
}

static bool tumo_sha_equals(const char* left, const char* right) {
    for(size_t i = 0; i < TUMO_SHA256_HEX_LEN; i++) {
        if(tumo_ascii_lower(left[i]) != tumo_ascii_lower(right[i])) {
            return false;
        }
    }
    return true;
}

static bool tumo_sha256_file(Storage* storage, const char* path, char out_hex[65]) {
    bool ok = false;
    File* file = storage_file_alloc(storage);
    uint8_t buffer[512];
    uint8_t digest[32];
    TumoSha256 sha;

    tumo_sha256_init(&sha);
    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        while(!storage_file_eof(file)) {
            size_t bytes_read = storage_file_read(file, buffer, sizeof(buffer));
            if(bytes_read == 0) {
                if(!storage_file_eof(file)) {
                    break;
                }
                continue;
            }
            tumo_sha256_update(&sha, buffer, bytes_read);
        }
        if(!storage_file_eof(file)) {
            break;
        }
        tumo_sha256_final(&sha, digest);

        for(size_t i = 0; i < sizeof(digest); i++) {
            snprintf(&out_hex[i * 2], 3, "%02x", digest[i]);
        }
        out_hex[64] = '\0';
        ok = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool tumo_state_value(FuriString* line, const char* key, FuriString* out) {
    if(!furi_string_start_with_str(line, key)) {
        return false;
    }

    furi_string_set(out, line);
    furi_string_right(out, strlen(key));
    furi_string_trim(out);
    return true;
}

static void tumo_read_package_state(Storage* storage, TumoPackageState* state) {
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    state->present =
        file_stream_open(stream, TUMO_PACKAGE_STATE_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    if(state->present) {
        while(stream_read_line(stream, line)) {
            if(furi_string_size(line) > TUMO_MAX_LINE_LEN) {
                state->parse_errors++;
                continue;
            }

            if(tumo_state_value(line, "ReleaseId:", state->release_id)) {
                continue;
            } else if(tumo_state_value(line, "Transaction:", state->transaction)) {
                continue;
            } else if(tumo_state_value(line, "Firmware:", state->firmware)) {
                continue;
            } else if(tumo_state_value(line, "PackageRelease:", state->package_release)) {
                continue;
            } else if(tumo_state_value(line, "Groups:", state->groups)) {
                continue;
            } else if(tumo_state_value(line, "InstalledFiles:", state->installed_files)) {
                continue;
            } else if(tumo_state_value(line, "CleanupCandidates:", state->cleanup_candidates)) {
                continue;
            } else if(tumo_state_value(line, "Rollback:", state->rollback)) {
                continue;
            }
        }
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_string_free(line);

    state->complete = state->present && !furi_string_empty(state->release_id) &&
                      !furi_string_empty(state->transaction) &&
                      !furi_string_empty(state->firmware) &&
                      !furi_string_empty(state->installed_files);
}

static bool tumo_json_string_value(const FuriString* line, const char* key, FuriString* out) {
    const char* text = furi_string_get_cstr(line);
    char pattern[24];

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = strstr(text, pattern);
    if(!key_pos) {
        return false;
    }

    const char* colon = strchr(key_pos + strlen(pattern), ':');
    if(!colon) {
        return false;
    }

    const char* start = strchr(colon, '"');
    if(!start) {
        return false;
    }
    start++;

    bool escaped = false;
    const char* end = start;
    while(*end) {
        if(!escaped && (*end == '"')) {
            break;
        }
        escaped = (!escaped && (*end == '\\'));
        if(*end != '\\') {
            escaped = false;
        }
        end++;
    }
    if(*end != '"') {
        return false;
    }

    furi_string_set_strn(out, start, end - start);
    return true;
}

static void tumo_audit_target(
    Storage* storage,
    const FuriString* target,
    const FuriString* expected_sha,
    TumoAuditStats* stats) {
    stats->files_expected++;
    if(stats->files_checked >= TUMO_MAX_AUDIT_FILES) {
        stats->capped = true;
        return;
    }
    stats->files_checked++;

    const char* target_path = furi_string_get_cstr(target);
    const char* sha = furi_string_get_cstr(expected_sha);
    if(!tumo_path_is_safe_ext_file(target_path) || !tumo_sha_is_valid(sha)) {
        stats->invalid_paths++;
        return;
    }

    if(!tumo_path_is_file(storage, target_path)) {
        stats->missing++;
        return;
    }

    char actual_sha[65];
    if(!tumo_sha256_file(storage, target_path, actual_sha)) {
        stats->unreadable++;
        return;
    }

    if(tumo_sha_equals(actual_sha, sha)) {
        stats->hash_ok++;
    } else {
        stats->mismatched++;
    }
}

static void tumo_audit_install_state(Storage* storage, TumoAuditStats* stats) {
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    FuriString* pending_target = furi_string_alloc();
    FuriString* pending_sha = furi_string_alloc();

    stats->install_state_present =
        file_stream_open(stream, TUMO_INSTALL_STATE_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    if(stats->install_state_present) {
        while(stream_read_line(stream, line)) {
            if(furi_string_size(line) > TUMO_MAX_LINE_LEN) {
                stats->line_too_long = true;
                stats->parse_errors++;
                continue;
            }

            if(tumo_json_string_value(line, "target", pending_target) &&
               furi_string_empty(pending_target)) {
                stats->parse_errors++;
            } else if(tumo_json_string_value(line, "sha256", pending_sha) &&
                      furi_string_empty(pending_sha)) {
                stats->parse_errors++;
            }

            if(!furi_string_empty(pending_target) && !furi_string_empty(pending_sha)) {
                tumo_audit_target(storage, pending_target, pending_sha, stats);
                furi_string_reset(pending_target);
                furi_string_reset(pending_sha);
            }
        }
    }

    if(!furi_string_empty(pending_target) || !furi_string_empty(pending_sha)) {
        stats->parse_errors++;
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_string_free(pending_sha);
    furi_string_free(pending_target);
    furi_string_free(line);
}

static void tumo_audit_legacy_paths(Storage* storage, TumoAuditStats* stats) {
    for(size_t i = 0; i < COUNT_OF(tumo_legacy_paths); i++) {
        if(!tumo_path_is_file(storage, tumo_legacy_paths[i].legacy)) {
            continue;
        }

        if(tumo_path_is_file(storage, tumo_legacy_paths[i].canonical)) {
            stats->legacy_duplicates++;
        } else {
            stats->legacy_review++;
        }
    }
}

static void tumo_packages_show_state(TumoPackagesApp* app) {
    TumoPackageState state;
    tumo_state_init(&state);
    tumo_read_package_state(app->storage, &state);

    furi_string_set_str(app->text, "Package state\n\n");
    if(!state.present) {
        furi_string_cat_printf(app->text, "State: missing\nPath: %s\n", TUMO_PACKAGE_STATE_PATH);
    } else {
        furi_string_cat_printf(app->text, "State: %s\n", state.complete ? "OK" : "partial");
        furi_string_cat_printf(
            app->text, "Release: %s\n", furi_string_get_cstr(state.release_id));
        furi_string_cat_printf(
            app->text, "Package: %s\n", furi_string_get_cstr(state.package_release));
        furi_string_cat_printf(app->text, "FW: %s\n", furi_string_get_cstr(state.firmware));
        furi_string_cat_printf(app->text, "Groups: %s\n", furi_string_get_cstr(state.groups));
        furi_string_cat_printf(
            app->text, "Installed: %s\n", furi_string_get_cstr(state.installed_files));
        furi_string_cat_printf(
            app->text, "Cleanup candidates: %s\n",
            furi_string_get_cstr(state.cleanup_candidates));
        furi_string_cat_printf(
            app->text, "Rollback: %s\n", furi_string_get_cstr(state.rollback));
        if(state.parse_errors) {
            furi_string_cat_printf(app->text, "Parse warnings: %u\n", state.parse_errors);
        }
    }

    tumo_state_free(&state);
    tumo_packages_set_text(app);
}

static void tumo_packages_show_audit(TumoPackagesApp* app) {
    TumoPackageState state;
    TumoAuditStats stats = {0};
    tumo_state_init(&state);
    tumo_read_package_state(app->storage, &state);
    stats.package_state_present = state.present;
    stats.parse_errors += state.parse_errors;

    tumo_audit_install_state(app->storage, &stats);
    tumo_audit_legacy_paths(app->storage, &stats);

    const bool clean = state.complete && stats.install_state_present && !stats.capped &&
                       !stats.line_too_long && (stats.parse_errors == 0) &&
                       (stats.invalid_paths == 0) && (stats.missing == 0) &&
                       (stats.mismatched == 0) && (stats.unreadable == 0) &&
                       (stats.legacy_duplicates == 0) && (stats.legacy_review == 0);

    furi_string_set_str(app->text, "Tumoflip package audit\n\n");
    furi_string_cat_printf(app->text, "Overall: %s\n", clean ? "OK" : "needs review");
    furi_string_cat_printf(
        app->text, "State file: %s\n", state.complete ? "OK" : "missing/partial");
    furi_string_cat_printf(
        app->text, "Install state: %s\n", stats.install_state_present ? "OK" : "missing");
    if(state.present) {
        furi_string_cat_printf(app->text, "FW: %s\n", furi_string_get_cstr(state.firmware));
        furi_string_cat_printf(app->text, "Groups: %s\n", furi_string_get_cstr(state.groups));
    }

    furi_string_cat_printf(app->text, "\nFiles\n");
    furi_string_cat_printf(app->text, "Expected: %u\n", stats.files_expected);
    furi_string_cat_printf(app->text, "Checked: %u\n", stats.files_checked);
    furi_string_cat_printf(app->text, "SHA OK: %u\n", stats.hash_ok);
    furi_string_cat_printf(app->text, "Missing: %u\n", stats.missing);
    furi_string_cat_printf(app->text, "SHA mismatch: %u\n", stats.mismatched);
    furi_string_cat_printf(app->text, "Read errors: %u\n", stats.unreadable);
    furi_string_cat_printf(app->text, "Invalid entries: %u\n", stats.invalid_paths);

    furi_string_cat_printf(app->text, "\nLegacy paths\n");
    furi_string_cat_printf(app->text, "Duplicates: %u\n", stats.legacy_duplicates);
    furi_string_cat_printf(app->text, "Review only: %u\n", stats.legacy_review);

    if(stats.parse_errors || stats.line_too_long || stats.capped) {
        furi_string_cat_printf(app->text, "\nParser\n");
        furi_string_cat_printf(app->text, "Warnings: %u\n", stats.parse_errors);
        if(stats.line_too_long) {
            furi_string_cat_printf(app->text, "Long lines: yes\n");
        }
        if(stats.capped) {
            furi_string_cat_printf(app->text, "Limit: first %u files\n", TUMO_MAX_AUDIT_FILES);
        }
    }

    furi_string_cat_printf(app->text, "\nRead-only. No cleanup is performed.");
    tumo_state_free(&state);
    tumo_packages_set_text(app);
}

static void tumo_packages_show_about(TumoPackagesApp* app) {
    furi_string_set_str(
        app->text,
        "Tumoflip Packages 0.1\n\n"
        "Read-only SD package audit.\n\n"
        "Checks package-state, install-state targets, SHA-256, and known legacy ARF paths.\n\n"
        "No files are deleted or overwritten.");
    tumo_packages_set_text(app);
}

static void tumo_packages_menu_callback(void* context, uint32_t index) {
    TumoPackagesApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool tumo_packages_custom_event_callback(void* context, uint32_t event) {
    TumoPackagesApp* app = context;

    switch(event) {
    case TumoPackagesEventAudit:
        tumo_packages_show_audit(app);
        return true;
    case TumoPackagesEventState:
        tumo_packages_show_state(app);
        return true;
    case TumoPackagesEventAbout:
        tumo_packages_show_about(app);
        return true;
    default:
        return false;
    }
}

static uint32_t tumo_packages_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t tumo_packages_nav_to_main(void* context) {
    UNUSED(context);
    return TumoPackagesViewMain;
}

static TumoPackagesApp* tumo_packages_app_alloc(void) {
    TumoPackagesApp* app = malloc(sizeof(TumoPackagesApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->text = furi_string_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tumo_packages_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "Tumoflip Packages");
    submenu_add_item(
        app->main_menu, "Run Audit", TumoPackagesMenuAudit, tumo_packages_menu_callback, app);
    submenu_add_item(
        app->main_menu, "State File", TumoPackagesMenuState, tumo_packages_menu_callback, app);
    submenu_add_item(
        app->main_menu, "About", TumoPackagesMenuAbout, tumo_packages_menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->main_menu), tumo_packages_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoPackagesViewMain, submenu_get_view(app->main_menu));

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), tumo_packages_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoPackagesViewText, text_box_get_view(app->text_box));

    view_dispatcher_switch_to_view(app->view_dispatcher, TumoPackagesViewMain);
    return app;
}

static void tumo_packages_app_free(TumoPackagesApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, TumoPackagesViewText);
    view_dispatcher_remove_view(app->view_dispatcher, TumoPackagesViewMain);
    text_box_free(app->text_box);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->text);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumoflip_packages_app(void* p) {
    UNUSED(p);

    TumoPackagesApp* app = tumo_packages_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    tumo_packages_app_free(app);

    return 0;
}
