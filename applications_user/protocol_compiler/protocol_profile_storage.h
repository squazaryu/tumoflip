#pragma once

#include "protocol_profile_core.h"

#include <storage/storage.h>

enum {
    ProtocolProfileMaximumProfiles = 8,
    ProtocolProfileMaximumFileSize = 2048,
    ProtocolProfileNameSize = 32,
    ProtocolProfileIdSize = 17,
    ProtocolProfileAmbiguitySize = 64,
    ProtocolProfileFilenameSize = 64,
};

#define PROTOCOL_PROFILE_DATA_DIR       EXT_PATH("apps_data/protocol_compiler")
#define PROTOCOL_PROFILE_DIRECTORY      PROTOCOL_PROFILE_DATA_DIR "/profiles"
#define PROTOCOL_PROFILE_DEMO_DIRECTORY PROTOCOL_PROFILE_DATA_DIR "/demo"
#define PROTOCOL_PROFILE_DEMO_CAPTURE   PROTOCOL_PROFILE_DEMO_DIRECTORY "/validation.sub"

typedef struct {
    ProtocolProfile profile;
    ProtocolProfileStatus status;
    char name[ProtocolProfileNameSize];
    char profile_id[ProtocolProfileIdSize];
    char ambiguity[ProtocolProfileAmbiguitySize];
    char filename[ProtocolProfileFilenameSize];
    uint8_t training_captures;
    uint8_t training_frames;
} ProtocolProfilePackage;

typedef struct {
    uint32_t frequency_hz;
    size_t pulse_count;
    bool truncated;
} ProtocolProfileCaptureInfo;

size_t protocol_profile_packages_load(
    Storage* storage,
    uint32_t current_api,
    ProtocolProfilePackage* packages,
    size_t capacity);

ProtocolProfileStatus protocol_profile_capture_load(
    Storage* storage,
    const char* path,
    int32_t* pulses,
    size_t capacity,
    ProtocolProfileCaptureInfo* info);
