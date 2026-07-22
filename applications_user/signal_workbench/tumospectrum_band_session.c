#include "tumospectrum_band_session.h"
#include "tumospectrum_storage.h"

#include <flipper_format/flipper_format.h>

#include <stdio.h>
#include <string.h>

#define TUMOSPECTRUM_BAND_SESSION_PATH         TUMOSPECTRUM_DATA_DIR "/band_map_session.ff"
#define TUMOSPECTRUM_BAND_SESSION_TEMP         TUMOSPECTRUM_DATA_DIR "/band_map_session.tmp"
#define TUMOSPECTRUM_BAND_SESSION_FILETYPE     "TumoSpectrum Band Session"
#define TUMOSPECTRUM_BAND_SESSION_VERSION      1U
#define TUMOSPECTRUM_BAND_SESSION_MAX_DELTA_HZ 150000U

static bool tumospectrum_band_session_path_valid(const char* path) {
    if(!path || strncmp(path, EXT_PATH("subghz/"), strlen(EXT_PATH("subghz/"))) != 0 ||
       strstr(path, "..") != NULL) {
        return false;
    }
    const size_t length = strlen(path);
    return length > strlen(".sub") && strcmp(path + length - strlen(".sub"), ".sub") == 0;
}

bool tumospectrum_band_session_load(Storage* storage, TumoSpectrumBandSession* session) {
    if(!storage || !session) return false;
    memset(session, 0, sizeof(*session));
    if(!storage_file_exists(storage, TUMOSPECTRUM_BAND_SESSION_PATH)) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* path = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t sample_count = 0U;
    bool valid = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOSPECTRUM_BAND_SESSION_PATH) ||
           !flipper_format_read_header(format, header, &version) ||
           version != TUMOSPECTRUM_BAND_SESSION_VERSION ||
           furi_string_cmp_str(header, TUMOSPECTRUM_BAND_SESSION_FILETYPE) != 0 ||
           !flipper_format_read_uint32(format, "Frequency", &session->frequency_hz, 1U) ||
           !flipper_format_read_uint32(format, "Sample count", &sample_count, 1U) ||
           sample_count > TUMOSPECTRUM_BAND_SESSION_MAX_SAMPLES) {
            break;
        }
        for(uint32_t index = 0U; index < sample_count; index++) {
            char key[16];
            snprintf(key, sizeof(key), "Sample %lu", (unsigned long)index);
            if(!flipper_format_read_string(format, key, path) ||
               !tumospectrum_band_session_path_valid(furi_string_get_cstr(path)) ||
               !storage_file_exists(storage, furi_string_get_cstr(path))) {
                break;
            }
            strlcpy(
                session->sample_paths[index],
                furi_string_get_cstr(path),
                sizeof(session->sample_paths[index]));
            session->sample_count++;
        }
        valid = session->frequency_hz > 0U && session->sample_count == sample_count;
    } while(false);
    flipper_format_free(format);
    furi_string_free(path);
    furi_string_free(header);
    if(!valid) memset(session, 0, sizeof(*session));
    return valid;
}

bool tumospectrum_band_session_save(Storage* storage, const TumoSpectrumBandSession* session) {
    if(!storage || !session || session->frequency_hz == 0U || session->sample_count == 0U ||
       session->sample_count > TUMOSPECTRUM_BAND_SESSION_MAX_SAMPLES ||
       !tumospectrum_storage_prepare(storage)) {
        return false;
    }
    for(size_t index = 0U; index < session->sample_count; index++) {
        if(!tumospectrum_band_session_path_valid(session->sample_paths[index])) return false;
    }

    storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_TEMP);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    const uint32_t sample_count = (uint32_t)session->sample_count;
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, TUMOSPECTRUM_BAND_SESSION_TEMP) ||
           !flipper_format_write_header_cstr(
               format, TUMOSPECTRUM_BAND_SESSION_FILETYPE, TUMOSPECTRUM_BAND_SESSION_VERSION) ||
           !flipper_format_write_uint32(format, "Frequency", &session->frequency_hz, 1U) ||
           !flipper_format_write_uint32(format, "Sample count", &sample_count, 1U)) {
            break;
        }
        success = true;
        for(size_t index = 0U; index < session->sample_count; index++) {
            char key[16];
            snprintf(key, sizeof(key), "Sample %u", (unsigned int)index);
            if(!flipper_format_write_string_cstr(format, key, session->sample_paths[index])) {
                success = false;
                break;
            }
        }
        if(success) success = flipper_format_file_close(format);
    } while(false);
    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_TEMP);
        return false;
    }

    storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_PATH);
    if(storage_common_rename(
           storage, TUMOSPECTRUM_BAND_SESSION_TEMP, TUMOSPECTRUM_BAND_SESSION_PATH) != FSE_OK) {
        storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_TEMP);
        return false;
    }
    return true;
}

bool tumospectrum_band_session_append(
    Storage* storage,
    uint32_t frequency_hz,
    const char* path,
    TumoSpectrumBandSession* session) {
    if(!storage || !session || frequency_hz == 0U || !tumospectrum_band_session_path_valid(path)) {
        return false;
    }

    TumoSpectrumBandSession current = {0};
    const bool loaded = tumospectrum_band_session_load(storage, &current);
    const uint32_t delta = loaded && current.frequency_hz > frequency_hz ?
                               current.frequency_hz - frequency_hz :
                           loaded ? frequency_hz - current.frequency_hz :
                                    0U;
    if(!loaded || delta > TUMOSPECTRUM_BAND_SESSION_MAX_DELTA_HZ ||
       current.sample_count >= TUMOSPECTRUM_BAND_SESSION_MAX_SAMPLES) {
        memset(&current, 0, sizeof(current));
        current.frequency_hz = frequency_hz;
    }

    for(size_t index = 0U; index < current.sample_count; index++) {
        if(strcmp(current.sample_paths[index], path) == 0) {
            *session = current;
            return true;
        }
    }
    strlcpy(
        current.sample_paths[current.sample_count],
        path,
        sizeof(current.sample_paths[current.sample_count]));
    current.sample_count++;
    if(!tumospectrum_band_session_save(storage, &current)) return false;
    *session = current;
    return true;
}

bool tumospectrum_band_session_clear(Storage* storage) {
    if(!storage) return false;
    storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_TEMP);
    const FS_Error result = storage_common_remove(storage, TUMOSPECTRUM_BAND_SESSION_PATH);
    return result == FSE_OK || result == FSE_NOT_EXIST;
}
