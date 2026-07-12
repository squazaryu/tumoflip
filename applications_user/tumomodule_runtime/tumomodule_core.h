#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TumoModuleManifestVersion = 1,
    TumoModuleMaximumPackages = 8,
    TumoModuleMaximumManifestSize = 1024,
    TumoModuleIdSize = 24,
    TumoModuleNameSize = 32,
    TumoModuleAdapterNameSize = 24,
};

typedef enum {
    TumoModuleBusNone = 0,
    TumoModuleBusI2cExternal,
    TumoModuleBusUartUsart,
} TumoModuleBus;

typedef enum {
    TumoModuleAdapterNone = 0,
    TumoModuleAdapterBme280I2c,
    TumoModuleAdapterTumoVgmUart,
} TumoModuleAdapter;

typedef enum {
    TumoModuleResourceNone = 0,
    TumoModuleResourceI2cExternal = 1U << 0,
    TumoModuleResourceUartUsart = 1U << 1,
    TumoModuleResourceExpansion = 1U << 2,
    TumoModuleResourcePin13 = 1U << 3,
    TumoModuleResourcePin14 = 1U << 4,
    TumoModuleResourcePin15 = 1U << 5,
    TumoModuleResourcePin16 = 1U << 6,
    TumoModuleResourceOtgPower = 1U << 7,
} TumoModuleResource;

typedef enum {
    TumoModuleManifestOk = 0,
    TumoModuleManifestInvalidArgument,
    TumoModuleManifestUnsupportedVersion,
    TumoModuleManifestInvalidIdentity,
    TumoModuleManifestApiMismatch,
    TumoModuleManifestUntrustedAdapter,
    TumoModuleManifestUnsafeConfiguration,
    TumoModuleManifestExternalPowerForbidden,
} TumoModuleManifestStatus;

typedef struct {
    uint32_t version;
    const char* id;
    const char* name;
    const char* adapter;
    const char* bus;
    uint32_t minimum_api;
    uint32_t maximum_api;
    uint32_t address;
    uint32_t baud;
    bool external_power;
} TumoModuleManifestInput;

typedef struct {
    uint32_t version;
    char id[TumoModuleIdSize];
    char name[TumoModuleNameSize];
    char adapter_name[TumoModuleAdapterNameSize];
    TumoModuleAdapter adapter;
    TumoModuleBus bus;
    uint32_t minimum_api;
    uint32_t maximum_api;
    uint32_t address;
    uint32_t baud;
    uint32_t resources;
} TumoModuleManifest;

typedef struct {
    uint32_t active_resources;
    uint32_t owner;
} TumoModuleResourceRegistry;

TumoModuleManifestStatus tumomodule_manifest_build(
    const TumoModuleManifestInput* input,
    uint32_t current_api,
    TumoModuleManifest* output);

const char* tumomodule_manifest_status_name(TumoModuleManifestStatus status);
const char* tumomodule_bus_name(TumoModuleBus bus);
const char* tumomodule_adapter_name(TumoModuleAdapter adapter);

void tumomodule_resource_registry_init(TumoModuleResourceRegistry* registry);
bool tumomodule_resource_claim(
    TumoModuleResourceRegistry* registry,
    uint32_t owner,
    uint32_t resources);
bool tumomodule_resource_release(TumoModuleResourceRegistry* registry, uint32_t owner);
