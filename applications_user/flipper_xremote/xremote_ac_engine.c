/*!
 *  @file flipper-xremote/xremote_ac_engine.c
 *  @license This project is released under the GNU GPLv3 License
 *
 * @brief Smart A/C profile helpers for Tumo XRemote.
 */

#include "xremote_ac_engine.h"

#include <dolphin/dolphin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool xremote_ac_prepare_storage(Storage* storage) {
    xremote_app_assert(storage, false);
    storage_simply_mkdir(storage, XREMOTE_AC_DIR);
    return true;
}

void xremote_ac_signal_name(char* out, size_t out_size, const char* preset, uint8_t temp) {
    snprintf(out, out_size, "%s %u", preset, temp);
}

bool xremote_ac_name_parse(const char* name, char* preset, size_t preset_size, uint8_t* temp) {
    xremote_app_assert(name, false);
    xremote_app_assert(preset, false);
    xremote_app_assert(temp, false);

    const char* space = strrchr(name, ' ');
    if(!space || space == name) return false;

    char* end = NULL;
    const long value = strtol(space + 1, &end, 10);
    if(!end || *end != '\0' || end == space + 1) return false;
    if(value < XREMOTE_AC_TEMP_BASE || value >= XREMOTE_AC_TEMP_BASE + XREMOTE_AC_TEMP_SLOTS)
        return false;

    size_t len = (size_t)(space - name);
    if(len >= preset_size) len = preset_size - 1;
    memcpy(preset, name, len);
    preset[len] = '\0';
    *temp = (uint8_t)value;
    return true;
}

static bool xremote_ac_open_read(FlipperFormat* ff, FuriString* scratch, const char* path) {
    uint32_t version = 0;
    if(!flipper_format_buffered_file_open_existing(ff, path)) return false;
    if(!flipper_format_read_header(ff, scratch, &version)) return false;
    return furi_string_equal(scratch, "IR signals file") && version == 1;
}

bool xremote_ac_scan_file(Storage* storage, const char* path, XRemoteAcIndex* index) {
    xremote_app_assert(storage, false);
    xremote_app_assert(path, false);
    xremote_app_assert(index, false);

    memset(index, 0, sizeof(*index));
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* scratch = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    bool success = false;

    if(xremote_ac_open_read(ff, scratch, path)) {
        char preset[XREMOTE_AC_PRESET_NAME_LEN];
        uint8_t temp = 0;

        while(flipper_format_read_string(ff, "name", name)) {
            const char* current = furi_string_get_cstr(name);
            if(strcmp(current, XREMOTE_AC_OFF_NAME) == 0) {
                index->has_off = true;
                success = true;
            } else if(xremote_ac_name_parse(current, preset, sizeof(preset), &temp)) {
                XRemoteAcPresetInfo* info = NULL;
                for(uint8_t i = 0; i < index->preset_count; i++) {
                    if(strcmp(index->presets[i].name, preset) == 0) {
                        info = &index->presets[i];
                        break;
                    }
                }

                if(!info && index->preset_count < XREMOTE_AC_MAX_PRESETS) {
                    info = &index->presets[index->preset_count++];
                    snprintf(info->name, sizeof(info->name), "%s", preset);
                }

                if(info) {
                    info->temp_bits |= 1UL << (temp - XREMOTE_AC_TEMP_BASE);
                    success = true;
                }
            }
        }
    }

    furi_string_free(name);
    furi_string_free(scratch);
    flipper_format_free(ff);
    return success && index->preset_count > 0;
}

bool xremote_ac_send_named_signal(
    XRemoteAppContext* app_ctx,
    Storage* storage,
    const char* path,
    const char* name) {
    xremote_app_assert(app_ctx, false);
    xremote_app_assert(storage, false);
    xremote_app_assert(path, false);
    xremote_app_assert(name, false);

    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* scratch = furi_string_alloc();
    FuriString* signal_name = furi_string_alloc_set_str(name);
    InfraredSignal* signal = infrared_signal_alloc();
    bool success = false;

    if(xremote_ac_open_read(ff, scratch, path) &&
       infrared_signal_search_and_read(signal, ff, signal_name)) {
        success = xremote_app_send_signal(app_ctx, signal);
        if(success) dolphin_deed(DolphinDeedIrSend);
    }

    infrared_signal_free(signal);
    furi_string_free(signal_name);
    furi_string_free(scratch);
    flipper_format_free(ff);
    return success;
}

int32_t xremote_ac_temp_bits_lowest(uint32_t bits) {
    for(uint8_t i = 0; i < XREMOTE_AC_TEMP_SLOTS; i++) {
        if(bits & (1UL << i)) return XREMOTE_AC_TEMP_BASE + i;
    }
    return -1;
}

int32_t xremote_ac_temp_bits_next(uint32_t bits, uint8_t temp, bool up) {
    if(bits == 0) return -1;

    int32_t current = temp - XREMOTE_AC_TEMP_BASE;
    if(current < 0) current = 0;
    if(current >= XREMOTE_AC_TEMP_SLOTS) current = XREMOTE_AC_TEMP_SLOTS - 1;

    for(uint8_t step = 1; step <= XREMOTE_AC_TEMP_SLOTS; step++) {
        int32_t candidate = up ? current + step : current - step;
        while(candidate < 0) candidate += XREMOTE_AC_TEMP_SLOTS;
        candidate %= XREMOTE_AC_TEMP_SLOTS;
        if(bits & (1UL << candidate)) return XREMOTE_AC_TEMP_BASE + candidate;
    }

    return -1;
}

int32_t xremote_ac_temp_bits_nearest(uint32_t bits, uint8_t temp) {
    if(bits == 0) return -1;

    int32_t current = temp - XREMOTE_AC_TEMP_BASE;
    if(current < 0) current = 0;
    if(current >= XREMOTE_AC_TEMP_SLOTS) current = XREMOTE_AC_TEMP_SLOTS - 1;

    if(bits & (1UL << current)) return XREMOTE_AC_TEMP_BASE + current;

    for(uint8_t step = 1; step < XREMOTE_AC_TEMP_SLOTS; step++) {
        int32_t up = (current + step) % XREMOTE_AC_TEMP_SLOTS;
        int32_t down = current - step;
        while(down < 0) down += XREMOTE_AC_TEMP_SLOTS;
        if(bits & (1UL << up)) return XREMOTE_AC_TEMP_BASE + up;
        if(bits & (1UL << down)) return XREMOTE_AC_TEMP_BASE + down;
    }

    return -1;
}
