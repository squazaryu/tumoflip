#pragma once

#include "protocol_profile_core.h"

enum {
    ProtocolProfileMaximumProfiles = 8,
    ProtocolProfileMaximumFileSize = 2048,
    ProtocolProfileNameSize = 32,
    ProtocolProfileIdSize = 17,
    ProtocolProfileAmbiguitySize = 64,
    ProtocolProfileFilenameSize = 64,
};

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
