#include "../rolljam_protocol_plugins.h"
#include "../scher_khan.h"
#include "../kia_v0.h"
#include "../kia_v2.h"
#include "../kia_v3_v4.h"
#include "../kia_v5.h"
#include "../kia_v6.h"
#include "../kia_v7.h"
#include "../ford_v1.h"
#include "../ford_v2.h"
#include "../ford_v3.h"
#include "../honda_static.h"


static const SubGhzProtocol* const rolljam_protocol_registry_fm_items[] = {
    &ford_protocol_v1,
    &subghz_protocol_scher_khan,
    &kia_protocol_v0,
    &kia_protocol_v2,
    &kia_protocol_v3_v4,
    &kia_protocol_v5,
    &kia_protocol_v6,
    &ford_protocol_v2,
    &ford_protocol_v3,
    &honda_static_protocol,
    &kia_protocol_v7,
};

static const SubGhzProtocolRegistry rolljam_protocol_registry_fm = {
    .items = rolljam_protocol_registry_fm_items,
    .size = sizeof(rolljam_protocol_registry_fm_items) /
            sizeof(rolljam_protocol_registry_fm_items[0]),
};

const RollJamProtocolPlugin rolljam_fm_plugin = {
    .plugin_name = "RollJam FM Registry",
    .filter = RollJamProtocolRegistryFilterFM,
    .registry = &rolljam_protocol_registry_fm,
};

static const FlipperAppPluginDescriptor rolljam_fm_plugin_descriptor = {
    .appid = ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID,
    .ep_api_version = ROLLJAM_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &rolljam_fm_plugin,
};

const FlipperAppPluginDescriptor* rolljam_fm_plugin_ep(void) {
    return &rolljam_fm_plugin_descriptor;
}
