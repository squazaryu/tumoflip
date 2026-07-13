#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TumoVmPeripheralManifestVersion = 1,
    TumoVmPeripheralMaximumPackages = 4,
    TumoVmPeripheralMaximumManifestSize = 1024,
    TumoVmPeripheralIdSize = 24,
    TumoVmPeripheralNameSize = 32,
    TumoVmPeripheralAidMaximum = 16,
    TumoVmPeripheralStateMaximum = 64,
};

typedef enum {
    TumoVmPeripheralAdapterNone = 0,
    TumoVmPeripheralAdapterUsbHidConsumer,
    TumoVmPeripheralAdapterNfcType4,
} TumoVmPeripheralAdapter;

typedef enum {
    TumoVmPeripheralActionNone = 0,
    TumoVmPeripheralActionPlayPause,
    TumoVmPeripheralActionVolumeUp,
    TumoVmPeripheralActionVolumeDown,
    TumoVmPeripheralActionStateV1,
} TumoVmPeripheralAction;

typedef enum {
    TumoVmPeripheralResourceNone = 0,
    TumoVmPeripheralResourceUsb = 1U << 0,
    TumoVmPeripheralResourceNfc = 1U << 1,
} TumoVmPeripheralResource;

typedef enum {
    TumoVmPeripheralManifestOk = 0,
    TumoVmPeripheralManifestInvalidArgument,
    TumoVmPeripheralManifestUnsupportedVersion,
    TumoVmPeripheralManifestInvalidIdentity,
    TumoVmPeripheralManifestApiMismatch,
    TumoVmPeripheralManifestUntrustedAdapter,
    TumoVmPeripheralManifestUntrustedAction,
    TumoVmPeripheralManifestUnsafeConfiguration,
    TumoVmPeripheralManifestConfirmationRequired,
} TumoVmPeripheralManifestStatus;

typedef struct {
    uint32_t version;
    const char* id;
    const char* name;
    const char* adapter;
    const char* action;
    uint32_t minimum_api;
    uint32_t maximum_api;
    const uint8_t* aid;
    size_t aid_size;
    const uint8_t* initial_state;
    size_t state_size;
    bool require_confirmation;
} TumoVmPeripheralManifestInput;

typedef struct {
    uint32_t version;
    char id[TumoVmPeripheralIdSize];
    char name[TumoVmPeripheralNameSize];
    TumoVmPeripheralAdapter adapter;
    TumoVmPeripheralAction action;
    uint32_t resources;
    uint8_t aid[TumoVmPeripheralAidMaximum];
    uint8_t aid_size;
    uint8_t initial_state[TumoVmPeripheralStateMaximum];
    uint8_t state_size;
    bool require_confirmation;
} TumoVmPeripheralManifest;

typedef struct {
    uint32_t claimed;
} TumoVmPeripheralResourceRegistry;

TumoVmPeripheralManifestStatus tumovm_peripheral_manifest_build(
    const TumoVmPeripheralManifestInput* input,
    uint32_t current_api,
    TumoVmPeripheralManifest* manifest);

void tumovm_peripheral_resources_init(TumoVmPeripheralResourceRegistry* registry);
bool tumovm_peripheral_resource_claim(
    TumoVmPeripheralResourceRegistry* registry,
    uint32_t resources);
void tumovm_peripheral_resource_release(
    TumoVmPeripheralResourceRegistry* registry,
    uint32_t resources);

const char* tumovm_peripheral_manifest_status_name(TumoVmPeripheralManifestStatus status);
const char* tumovm_peripheral_adapter_name(TumoVmPeripheralAdapter adapter);
const char* tumovm_peripheral_action_name(TumoVmPeripheralAction action);
