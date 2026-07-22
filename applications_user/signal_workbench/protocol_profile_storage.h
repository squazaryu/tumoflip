#pragma once

#include "protocol_profile_package.h"

#include <storage/storage.h>

#define PROTOCOL_PROFILE_DATA_DIR       EXT_PATH("apps_data/signal_workbench")
#define PROTOCOL_PROFILE_DIRECTORY      PROTOCOL_PROFILE_DATA_DIR "/profiles"
#define PROTOCOL_PROFILE_DEMO_DIRECTORY PROTOCOL_PROFILE_DATA_DIR "/demo"
#define PROTOCOL_PROFILE_DEMO_FILENAME  "demo_pulse_pair.tproto"
#define PROTOCOL_PROFILE_DEMO_CAPTURE   PROTOCOL_PROFILE_DEMO_DIRECTORY "/validation.sub"
#define PROTOCOL_PROFILE_OBSERVATIONS   PROTOCOL_PROFILE_DATA_DIR "/protocol_observations.csv"

#define PROTOCOL_PROFILE_LEGACY_DATA_DIR  EXT_PATH("apps_data/protocol_compiler")
#define PROTOCOL_PROFILE_LEGACY_DIRECTORY PROTOCOL_PROFILE_LEGACY_DATA_DIR "/profiles"
#define PROTOCOL_PROFILE_LEGACY_DEMO      PROTOCOL_PROFILE_LEGACY_DATA_DIR "/demo/validation.sub"

typedef struct {
    uint32_t frequency_hz;
    size_t pulse_count;
    bool truncated;
} ProtocolProfileCaptureInfo;

bool protocol_profile_storage_prepare(Storage* storage);

size_t protocol_profile_packages_load(
    Storage* storage,
    uint32_t current_api,
    ProtocolProfilePackage* packages,
    size_t capacity);

bool protocol_profile_package_save(
    Storage* storage,
    const ProtocolProfilePackage* package,
    char* saved_path,
    size_t saved_path_size);

bool protocol_profile_package_delete(Storage* storage, const ProtocolProfilePackage* package);

ProtocolProfileStatus protocol_profile_capture_load(
    Storage* storage,
    const char* path,
    int32_t* pulses,
    size_t capacity,
    ProtocolProfileCaptureInfo* info);

bool protocol_profile_observation_append(
    Storage* storage,
    const ProtocolProfilePackage* package,
    const ProtocolProfileDecodeResult* result,
    uint64_t changed_mask,
    uint32_t match_count);
