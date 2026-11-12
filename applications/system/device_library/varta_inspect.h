#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {VartaIdOnly, VartaMissingBlocks, VartaBadFrame, VartaDumpDecoded} VartaInspectStatus;
typedef struct {
    uint8_t id[5];
    uint8_t clock;
    VartaInspectStatus status;
    bool known_prefix;
    bool has_config;
    bool config_matches;
    bool has_marker;
    bool marker_matches;
} VartaInspection;

bool varta_inspect_frame(uint32_t high, uint32_t low, uint8_t id[5]);
bool varta_inspect_read_list(const char* text, uint8_t* mask);
void varta_inspect_id(VartaInspection* result, const uint8_t id[5], uint8_t clock);
void varta_inspect_dump(VartaInspection* result, const uint32_t blocks[8], uint8_t mask);
