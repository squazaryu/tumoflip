#include "nfc_location_sidecar_plugin.h"

#include <furi.h>
#include <tumoflip_device_services/tumoflip_device_services.h>

#define TAG "NfcLocationSidecar"

struct NfcLocationSidecarSession {
    Storage* storage;
    FuriString* saved_path;
    TumoflipDeviceServicesClient* client;
};

static void
    nfc_location_sidecar_callback(const TumoflipDeviceServicesResult* result, void* context) {
    NfcLocationSidecarSession* session = context;
    if(result->request == TumoflipDeviceServicesRequestLocation &&
       result->code == TumoflipDeviceServicesResultOk && !furi_string_empty(session->saved_path)) {
        if(!tumoflip_device_services_write_sidecar(
               session->storage,
               furi_string_get_cstr(session->saved_path),
               "nfc",
               &result->value.location)) {
            FURI_LOG_W(TAG, "Could not write optional location sidecar");
        }
    }
}

static void nfc_location_sidecar_stop(NfcLocationSidecarSession* session) {
    if(!session) return;
    tumoflip_device_services_client_cancel(session->client);
    tumoflip_device_services_client_free(session->client);
    furi_string_free(session->saved_path);
    free(session);
}

static NfcLocationSidecarSession*
    nfc_location_sidecar_start(Storage* storage, const char* saved_path) {
    if(!storage || !saved_path || !saved_path[0]) return NULL;

    NfcLocationSidecarSession* session = malloc(sizeof(NfcLocationSidecarSession));
    if(!session) return NULL;
    memset(session, 0, sizeof(NfcLocationSidecarSession));
    session->storage = storage;
    session->saved_path = furi_string_alloc_set(saved_path);
    session->client =
        tumoflip_device_services_client_alloc(nfc_location_sidecar_callback, session);
    if(!session->client ||
       !tumoflip_device_services_client_request_location(session->client, "sidecar")) {
        nfc_location_sidecar_stop(session);
        return NULL;
    }

    return session;
}

static const NfcLocationSidecarPlugin nfc_location_sidecar_plugin = {
    .start = nfc_location_sidecar_start,
    .stop = nfc_location_sidecar_stop,
};

static const FlipperAppPluginDescriptor nfc_location_sidecar_plugin_descriptor = {
    .appid = NFC_LOCATION_SIDECAR_PLUGIN_APP_ID,
    .ep_api_version = NFC_LOCATION_SIDECAR_PLUGIN_API_VERSION,
    .entry_point = &nfc_location_sidecar_plugin,
};

const FlipperAppPluginDescriptor* nfc_location_sidecar_plugin_ep(void) {
    return &nfc_location_sidecar_plugin_descriptor;
}
