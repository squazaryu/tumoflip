#include "protopirate_psa_bf_plugin.h"

static void plugin_set_host_api(const ProtoPiratePsaBfHostApi* api) {
    (void)api;
}

static bool plugin_needs_bruteforce(void* app, ProtoPiratePsaBfContext ctx) {
    (void)app;
    (void)ctx;
    return false;
}

static bool plugin_is_running(void* app) {
    (void)app;
    return false;
}

static void plugin_on_scene_enter(void* app, ProtoPiratePsaBfContext ctx) {
    (void)app;
    (void)ctx;
}

static bool plugin_on_scene_event(void* app, ProtoPiratePsaBfContext ctx, SceneManagerEvent event) {
    (void)app;
    (void)ctx;
    (void)event;
    return false;
}

static void plugin_on_scene_exit(void* app, ProtoPiratePsaBfContext ctx) {
    (void)app;
    (void)ctx;
}

static bool plugin_widget_left_should_bruteforce(void* app, ProtoPiratePsaBfContext ctx) {
    (void)app;
    (void)ctx;
    return false;
}

static void plugin_context_release(void* app) {
    (void)app;
}

static const ProtoPiratePsaBfPlugin protopirate_psa_bf_plugin = {
    .plugin_name = "ProtoPirate PSA BF",
    .set_host_api = plugin_set_host_api,
    .needs_bruteforce = plugin_needs_bruteforce,
    .is_running = plugin_is_running,
    .on_scene_enter = plugin_on_scene_enter,
    .on_scene_event = plugin_on_scene_event,
    .on_scene_exit = plugin_on_scene_exit,
    .widget_left_should_bruteforce = plugin_widget_left_should_bruteforce,
    .context_release = plugin_context_release,
};

static const FlipperAppPluginDescriptor protopirate_psa_bf_plugin_descriptor = {
    .appid = PROTOPIRATE_PSA_BF_PLUGIN_APP_ID,
    .ep_api_version = PROTOPIRATE_PSA_BF_PLUGIN_API_VERSION,
    .entry_point = &protopirate_psa_bf_plugin,
};

const FlipperAppPluginDescriptor* protopirate_psa_bf_plugin_ep(void) {
    return &protopirate_psa_bf_plugin_descriptor;
}
