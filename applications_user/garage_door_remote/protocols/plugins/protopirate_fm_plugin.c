#include "../protopirate_protocol_plugins.h"
#include "../ansonic.h"

static const SubGhzProtocol* const protopirate_protocol_registry_fm_items[] = {
    &subghz_protocol_ansonic,
};

static const SubGhzProtocolRegistry protopirate_protocol_registry_fm = {
    .items = protopirate_protocol_registry_fm_items,
    .size = sizeof(protopirate_protocol_registry_fm_items) /
            sizeof(protopirate_protocol_registry_fm_items[0]),
};

static const ProtoPirateProtocolPlugin protopirate_fm_plugin = {
    .plugin_name = "Garage Door Remote FM Registry",
    .filter = ProtoPirateProtocolRegistryFilterFM,
    .registry = &protopirate_protocol_registry_fm,
};

static const FlipperAppPluginDescriptor protopirate_fm_plugin_descriptor = {
    .appid = PROTOPIRATE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = PROTOPIRATE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &protopirate_fm_plugin,
};

const FlipperAppPluginDescriptor* protopirate_fm_plugin_ep(void) {
    return &protopirate_fm_plugin_descriptor;
}
