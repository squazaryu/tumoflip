#include "../rolljam_protocol_plugins.h"
#include "../chrysler_v0.h"
#include "../fiat_v0.h"
#include "../fiat_v1.h"
#include "../ford_v0.h"
#include "../kia_v1.h"
#include "../porsche_touareg.h"
#include "../psa.h"
#include "../subaru.h"
#include "../vag.h"
#include "../star_line.h"
#include "../keeloq.h"
#include "../honda_v1.h"

static const SubGhzProtocol* const rolljam_protocol_registry_am_items[] = {
    &ford_protocol_v0,
    &subghz_protocol_keeloq,
    &chrysler_protocol_v0,
    &fiat_protocol_v0,
    &fiat_v1_protocol,
    &honda_v1_protocol,
    &kia_protocol_v1,
    &porsche_touareg_protocol,
    &psa_protocol,
    &subaru_protocol,
    &vag_protocol,
    &subghz_protocol_star_line,
};

static const SubGhzProtocolRegistry rolljam_protocol_registry_am = {
    .items = rolljam_protocol_registry_am_items,
    .size = sizeof(rolljam_protocol_registry_am_items) /
            sizeof(rolljam_protocol_registry_am_items[0]),
};

const RollJamProtocolPlugin rolljam_am_plugin = {
    .plugin_name = "RollJam AM Registry",
    .filter = RollJamProtocolRegistryFilterAM,
    .registry = &rolljam_protocol_registry_am,
};

static const FlipperAppPluginDescriptor rolljam_am_plugin_descriptor = {
    .appid = ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID,
    .ep_api_version = ROLLJAM_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &rolljam_am_plugin,
};

const FlipperAppPluginDescriptor* rolljam_am_plugin_ep(void) {
    return &rolljam_am_plugin_descriptor;
}
