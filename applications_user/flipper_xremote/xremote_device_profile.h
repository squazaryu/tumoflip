#pragma once

#include <furi.h>
#include <storage/storage.h>

#define XREMOTE_DEVICE_PROFILE_HEADER    "Tumo XRemote Device Profile"
#define XREMOTE_DEVICE_PROFILE_VERSION   1U
#define XREMOTE_DEVICE_PROFILE_EXTENSION ".tdevice"
#define XREMOTE_DEVICE_PROFILE_ROOT      EXT_PATH("apps_data/tumoflip_xremote")
#define XREMOTE_DEVICE_PROFILE_FOLDER    XREMOTE_DEVICE_PROFILE_ROOT "/devices"

#define XREMOTE_DEVICE_NAME_MAX       32U
#define XREMOTE_DEVICE_PATH_MAX       256U
#define XREMOTE_DEVICE_PROTOCOL_MAX   32U
#define XREMOTE_DEVICE_RF_COMMAND_MAX 8U

typedef enum {
    XRemoteDeviceRadioInternal,
    XRemoteDeviceRadioExternal,
} XRemoteDeviceRadio;

typedef enum {
    XRemoteDeviceAdapterReplayOnly,
    XRemoteDeviceAdapterPrinceton4,
} XRemoteDeviceAdapter;

typedef struct {
    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    char path[XREMOTE_DEVICE_PATH_MAX];
    char protocol[XREMOTE_DEVICE_PROTOCOL_MAX + 1U];
    XRemoteDeviceAdapter adapter;
} XRemoteDeviceRfCommand;

typedef struct {
    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    char path[XREMOTE_DEVICE_PATH_MAX];
    char ir_path[XREMOTE_DEVICE_PATH_MAX];
    XRemoteDeviceRadio radio;
    uint8_t rf_count;
    XRemoteDeviceRfCommand rf[XREMOTE_DEVICE_RF_COMMAND_MAX];
} XRemoteDeviceProfile;

XRemoteDeviceProfile* xremote_device_profile_alloc(void);
void xremote_device_profile_free(XRemoteDeviceProfile* profile);
void xremote_device_profile_reset(XRemoteDeviceProfile* profile);
bool xremote_device_profile_storage_ready(Storage* storage);

bool xremote_device_profile_load(Storage* storage, const char* path, XRemoteDeviceProfile* profile);
bool xremote_device_profile_store(Storage* storage, const XRemoteDeviceProfile* profile);
bool xremote_device_profile_create_path(
    Storage* storage,
    const char* preferred_name,
    char* output,
    size_t output_size);
bool xremote_device_profile_duplicate(
    Storage* storage,
    const XRemoteDeviceProfile* source,
    XRemoteDeviceProfile* duplicate);
bool xremote_device_profile_add_rf(
    XRemoteDeviceProfile* profile,
    const char* name,
    const char* path,
    const char* protocol,
    XRemoteDeviceAdapter adapter);
bool xremote_device_profile_rename(XRemoteDeviceProfile* profile, const char* name);
bool xremote_device_profile_set_ir(XRemoteDeviceProfile* profile, const char* path);
bool xremote_device_profile_rename_rf(
    XRemoteDeviceProfile* profile,
    uint8_t index,
    const char* name);
bool xremote_device_profile_move_rf(XRemoteDeviceProfile* profile, uint8_t from, uint8_t to);
bool xremote_device_profile_remove_rf(XRemoteDeviceProfile* profile, uint8_t index);
bool xremote_device_profile_delete(Storage* storage, const XRemoteDeviceProfile* profile);
bool xremote_device_profile_delete_path(Storage* storage, const char* path);

const char* xremote_device_radio_name(XRemoteDeviceRadio radio);
const char* xremote_device_adapter_name(XRemoteDeviceAdapter adapter);
XRemoteDeviceAdapter xremote_device_adapter_from_name(const char* name);
