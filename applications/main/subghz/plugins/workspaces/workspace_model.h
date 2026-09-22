#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WORKSPACE_NAME_SIZE   25U
#define WORKSPACE_PRESET_SIZE 32U
typedef struct {
    uint32_t frequency;
    uint32_t raw_frequency;
    char preset[WORKSPACE_PRESET_SIZE];
    char raw_preset[WORKSPACE_PRESET_SIZE];
    uint32_t pack;
    uint32_t hopping;
    uint32_t radio;
} WorkspaceProfile;

bool workspace_name_valid(const char* name);
bool workspace_profile_valid(const WorkspaceProfile* profile, uint32_t pack_count);
