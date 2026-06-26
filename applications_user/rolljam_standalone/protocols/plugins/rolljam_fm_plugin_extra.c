// protocols/plugins/rolljam_fm_plugin_extra.c
#include "../rolljam_protocol_plugins.h"
#include "../land_rover_v0.h"
#include "../mazda_v0.h"
#include "../mitsubishi_v0.h"
#include "../psa.h"

static const SubGhzProtocol* const rolljam_protocol_registry_fm_extra_items[] = {
    &land_rover_v0_protocol,
    &mazda_v0_protocol,
    &mitsubishi_v0_protocol,
    &psa_protocol,
};

static const SubGhzProtocolRegistry rolljam_protocol_registry_fm_extra = {
    .items = rolljam_protocol_registry_fm_extra_items,
    .size = sizeof(rolljam_protocol_registry_fm_extra_items) /
            sizeof(rolljam_protocol_registry_fm_extra_items[0]),
};

const RollJamProtocolPlugin rolljam_fm_plugin_extra = {
    .plugin_name = "RollJam FM Registry Extra",
    .filter = RollJamProtocolRegistryFilterFM,
    .registry = &rolljam_protocol_registry_fm_extra,
};

static const FlipperAppPluginDescriptor rolljam_fm_plugin_extra_descriptor = {
    .appid = "rolljam_fm_plugin_extra",
    .ep_api_version = ROLLJAM_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &rolljam_fm_plugin_extra,
};

const FlipperAppPluginDescriptor* rolljam_fm_plugin_extra_ep(void) {
    return &rolljam_fm_plugin_extra_descriptor;
}
