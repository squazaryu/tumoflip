#include <flipper_application/flipper_application.h>
#include <lfrfid/tools/hitags.h>

static const LFRFIDHitagSPlugin lfrfid_hitags_plugin = {
    .read_uid = hitags_read_uid,
    .write = hitags_write,
};

static const FlipperAppPluginDescriptor lfrfid_hitags_plugin_descriptor = {
    .appid = LFRFID_HITAGS_PLUGIN_APP_ID,
    .ep_api_version = LFRFID_HITAGS_PLUGIN_API_VERSION,
    .entry_point = &lfrfid_hitags_plugin,
};

const FlipperAppPluginDescriptor* lfrfid_hitags_plugin_ep(void) {
    return &lfrfid_hitags_plugin_descriptor;
}
