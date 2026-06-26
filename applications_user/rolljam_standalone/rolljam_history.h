// rolljam_history.h
#pragma once

#include <stddef.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/protocols/base.h>

#define ROLLJAM_HISTORY_MAX 10

typedef struct SubGhzEnvironment SubGhzEnvironment;
typedef struct RollJamHistory RollJamHistory;

typedef enum {
    RollJamHistorySourceUnknown = 0,
    RollJamHistorySourceExternal,
    RollJamHistorySourceInternal,
    RollJamHistorySourceCount,
} RollJamHistorySource;

RollJamHistory* rolljam_history_alloc(void);
void rolljam_history_free(RollJamHistory* instance);
void rolljam_history_reset(RollJamHistory* instance);
uint16_t rolljam_history_get_item(RollJamHistory* instance);
uint16_t rolljam_history_get_last_index(RollJamHistory* instance);
RollJamHistorySource rolljam_history_get_source(
    RollJamHistory* instance,
    uint16_t idx);
const char* rolljam_history_source_name(RollJamHistorySource source);
void rolljam_history_format_status_text(
    RollJamHistory* instance,
    char* output,
    size_t output_size);
void rolljam_history_get_status_text(RollJamHistory* instance, FuriString* output);

bool rolljam_history_get_capture_path(
    RollJamHistory* instance,
    uint16_t idx,
    FuriString* out_path);
bool rolljam_history_capture_path_equals(
    RollJamHistory* instance,
    uint16_t idx,
    const char* path);

bool rolljam_history_add_to_history(
    RollJamHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    RollJamHistorySource source);
void rolljam_history_delete_item(RollJamHistory* instance, uint16_t idx);
void rolljam_history_get_text_item_menu(
    RollJamHistory* instance,
    FuriString* output,
    uint16_t idx);
void rolljam_history_get_text_item_detail(
    RollJamHistory* instance,
    uint16_t idx,
    FuriString* output,
    SubGhzEnvironment* environment);
FlipperFormat* rolljam_history_get_raw_data(RollJamHistory* instance, uint16_t idx);

void rolljam_history_release_scratch(RollJamHistory* instance);

void rolljam_history_set_item_str(RollJamHistory* instance, uint16_t idx, const char* str);
