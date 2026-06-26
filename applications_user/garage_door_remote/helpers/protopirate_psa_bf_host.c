#include "protopirate_psa_bf_host.h"

bool protopirate_psa_bf_plugin_ensure_loaded(ProtoPirateApp* app) {
    (void)app;
    return false;
}

void protopirate_psa_bf_plugin_unload_if_idle(ProtoPirateApp* app) {
    (void)app;
}

void protopirate_psa_bf_context_release(ProtoPirateApp* app) {
    (void)app;
}
