#pragma once

#include <flipper_application/flipper_application.h>
#include <storage/storage.h>

#define NFC_LOCATION_SIDECAR_PLUGIN_APP_ID      "NfcLocationSidecarPlugin"
#define NFC_LOCATION_SIDECAR_PLUGIN_API_VERSION 1U

typedef struct NfcLocationSidecarSession NfcLocationSidecarSession;

typedef struct {
    NfcLocationSidecarSession* (*start)(Storage* storage, const char* saved_path);
    void (*stop)(NfcLocationSidecarSession* session);
} NfcLocationSidecarPlugin;

const FlipperAppPluginDescriptor* nfc_location_sidecar_plugin_ep(void);
