#pragma once

#include "../rolljam_app_i.h"
#include "../rolljam_history.h"
#include "../protocols/protocols_common.h"
// #include "../scenes/plugins/rolljam_psa_bf_plugin.h"

#define ROLLJAM_PSA_BF_HOST_TAG "RollJamPsaBfHost"
// #define PSA_BF_PLUGIN_PATH APP_ASSETS_PATH("plugins/rolljam_psa_bf_plugin.fal")

// bool rolljam_psa_bf_plugin_ensure_loaded(RollJamApp* app);
// void rolljam_psa_bf_plugin_unload_if_idle(RollJamApp* app);


void rolljam_receiver_info_rebuild_normal_widget(void* app);

#ifdef ENABLE_SUB_DECODE_SCENE
void rolljam_subdecode_psa_bf_complete_refresh(void* app);
#endif
