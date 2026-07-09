/*!
 *  @file flipper-xremote/xremote_ac_engine.h
 *  @license This project is released under the GNU GPLv3 License
 *
 * @brief Smart A/C profile helpers for Tumo XRemote.
 */

#pragma once

#include "xremote_app.h"

#define XREMOTE_AC_DIR              APP_DATA_PATH("ac")
#define XREMOTE_AC_FZ_AC_DIR        EXT_PATH("apps_data/fz_ac")
#define XREMOTE_AC_MAX_PRESETS      8
#define XREMOTE_AC_PRESET_NAME_LEN  16
#define XREMOTE_AC_TEMP_BASE        10
#define XREMOTE_AC_TEMP_SLOTS       32
#define XREMOTE_AC_OFF_NAME         "Off"

typedef struct {
    char name[XREMOTE_AC_PRESET_NAME_LEN];
    uint32_t temp_bits;
} XRemoteAcPresetInfo;

typedef struct {
    bool has_off;
    uint8_t preset_count;
    XRemoteAcPresetInfo presets[XREMOTE_AC_MAX_PRESETS];
} XRemoteAcIndex;

bool xremote_ac_prepare_storage(Storage* storage);
void xremote_ac_signal_name(char* out, size_t out_size, const char* preset, uint8_t temp);
bool xremote_ac_name_parse(const char* name, char* preset, size_t preset_size, uint8_t* temp);
bool xremote_ac_scan_file(Storage* storage, const char* path, XRemoteAcIndex* index);
bool xremote_ac_send_named_signal(
    XRemoteAppContext* app_ctx,
    Storage* storage,
    const char* path,
    const char* name);

int32_t xremote_ac_temp_bits_lowest(uint32_t bits);
int32_t xremote_ac_temp_bits_next(uint32_t bits, uint8_t temp, bool up);
int32_t xremote_ac_temp_bits_nearest(uint32_t bits, uint8_t temp);
