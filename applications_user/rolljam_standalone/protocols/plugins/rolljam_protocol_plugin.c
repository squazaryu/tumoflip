#include "../rolljam_protocol_plugins.h"
#include "../chrysler_v0.h"
#include "../fiat_v0.h"
#include "../fiat_v1.h"
#include "../ford_v0.h"
#include "../ford_v1.h"
#include "../ford_v2.h"
#include "../ford_v3.h"
#include "../honda_static.h"
#include "../honda_v1.h"
#include "../kia_v0.h"
#include "../kia_v1.h"
#include "../kia_v2.h"
#include "../kia_v3_v4.h"
#include "../kia_v5.h"
#include "../kia_v6.h"
#include "../kia_v7.h"
#include "../land_rover_v0.h"
#include "../mazda_v0.h"
#include "../mitsubishi_v0.h"
#include "../porsche_touareg.h"
#include "../psa.h"
#include "../scher_khan.h"
#include "../star_line.h"
#include "../subaru.h"
#include "../vag.h"
#include "../keeloq.h"

static const SubGhzProtocol* const rolljam_protocol_registry_items[] = {
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
    &land_rover_v0_protocol,
    &mazda_v0_protocol,
    &mitsubishi_v0_protocol,
    &kia_protocol_v7,
};

static const SubGhzProtocolRegistry rolljam_protocol_registry = {
    .items = rolljam_protocol_registry_items,
    .size = sizeof(rolljam_protocol_registry_items) / sizeof(rolljam_protocol_registry_items[0]),
};

const RollJamProtocolPlugin rolljam_protocol_plugin = {
    .plugin_name = "RollJam Protocol Registry",
    .filter = RollJamProtocolRegistryFilterAll,
    .registry = &rolljam_protocol_registry,
};

static const FlipperAppPluginDescriptor rolljam_protocol_plugin_descriptor = {
    .appid = ROLLJAM_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = ROLLJAM_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &rolljam_protocol_plugin,
};

const FlipperAppPluginDescriptor* rolljam_protocol_plugin_ep(void) {
    return &rolljam_protocol_plugin_descriptor;
}
