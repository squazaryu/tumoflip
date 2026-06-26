#pragma once

#include <furi.h>
#include <lib/flipper_format/flipper_format.h>
#include "rolljam_types.h"
#include "../rolljam_history.h"

typedef struct RollJamApp RollJamApp;

typedef struct RollJamApp RollJamApp;

void rolljam_selected_capture_set(RollJamApp* app, RollJamCaptureOwner owner, uint16_t index);
void rolljam_selected_capture_clear(RollJamApp* app);
bool rolljam_selected_capture_is_valid(RollJamApp* app);
uint16_t rolljam_selected_capture_get_index(RollJamApp* app);
RollJamHistory* rolljam_selected_capture_get_history(RollJamApp* app);
RollJamHistorySource rolljam_selected_capture_get_source(RollJamApp* app);
bool rolljam_selected_capture_get_path(RollJamApp* app, FuriString* out_path);
FlipperFormat* rolljam_selected_capture_get_raw_data(RollJamApp* app);
void rolljam_selected_capture_release_scratch(RollJamApp* app);
