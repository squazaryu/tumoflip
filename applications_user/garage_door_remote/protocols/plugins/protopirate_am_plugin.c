#include "../protopirate_protocol_plugins.h"

#include "../alutech_at_4n.h"
#include "../beninca_arc.h"
#include "../came.h"
#include "../came_atomo.h"
#include "../came_twee.h"
#include "../chamberlain_code.h"
#include "../clemsa.h"
#include "../dooya.h"
#include "../faac_slh.h"
#include "../gate_tx.h"
#include "../hormann.h"
#include "../keeloq.h"
#include "../linear.h"
#include "../linear_delta3.h"
#include "../megacode.h"
#include "../nice_flo.h"
#include "../nice_flor_s.h"
#include "../princeton.h"
#include "../somfy_keytis.h"
#include "../somfy_telis.h"

#define PROTOPIRATE_AM_PROTOCOL(symbol) 
#include "protopirate_am_protocols_list.inc"
#undef PROTOPIRATE_AM_PROTOCOL

static const SubGhzProtocol* const protopirate_protocol_registry_am_items[] = {
#define PROTOPIRATE_AM_PROTOCOL(symbol) &symbol,
#include "protopirate_am_protocols_list.inc"
#undef PROTOPIRATE_AM_PROTOCOL
};

static const SubGhzProtocolRegistry protopirate_protocol_registry_am = {
    .items = protopirate_protocol_registry_am_items,
    .size = sizeof(protopirate_protocol_registry_am_items) /
            sizeof(protopirate_protocol_registry_am_items[0]),
};

static const ProtoPirateProtocolPlugin protopirate_am_plugin = {
    .plugin_name = "Garage Door Remote AM Registry",
    .filter = ProtoPirateProtocolRegistryFilterAM,
    .registry = &protopirate_protocol_registry_am,
};

static const FlipperAppPluginDescriptor protopirate_am_plugin_descriptor = {
    .appid = PROTOPIRATE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = PROTOPIRATE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &protopirate_am_plugin,
};

const FlipperAppPluginDescriptor* protopirate_am_plugin_ep(void) {
    return &protopirate_am_plugin_descriptor;
}
