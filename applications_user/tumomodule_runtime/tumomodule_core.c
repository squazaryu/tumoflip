#include "tumomodule_core.h"

#include <string.h>

static bool
    tumomodule_copy_string(char* output, size_t output_size, const char* input, bool identifier) {
    if(output == NULL || output_size == 0 || input == NULL) return false;
    const size_t length = strlen(input);
    if(length == 0 || length >= output_size) return false;
    for(size_t index = 0; index < length; index++) {
        const uint8_t value = (uint8_t)input[index];
        if(identifier) {
            const bool valid = (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
                               value == '_' || value == '-';
            if(!valid) return false;
        } else if(value < 0x20 || value > 0x7E) {
            return false;
        }
    }
    memcpy(output, input, length + 1);
    return true;
}

static bool tumomodule_string_equal(const char* first, const char* second) {
    return first != NULL && second != NULL && strcmp(first, second) == 0;
}

TumoModuleManifestStatus tumomodule_manifest_build(
    const TumoModuleManifestInput* input,
    uint32_t current_api,
    TumoModuleManifest* output) {
    if(input == NULL || output == NULL || current_api == 0) {
        return TumoModuleManifestInvalidArgument;
    }
    memset(output, 0, sizeof(*output));
    if(input->version != TumoModuleManifestVersion) {
        return TumoModuleManifestUnsupportedVersion;
    }
    if(!tumomodule_copy_string(output->id, sizeof(output->id), input->id, true) ||
       !tumomodule_copy_string(output->name, sizeof(output->name), input->name, false) ||
       !tumomodule_copy_string(
           output->adapter_name, sizeof(output->adapter_name), input->adapter, true)) {
        return TumoModuleManifestInvalidIdentity;
    }
    if(input->minimum_api == 0 || input->maximum_api < input->minimum_api ||
       current_api < input->minimum_api || current_api > input->maximum_api) {
        return TumoModuleManifestApiMismatch;
    }
    if(input->external_power) return TumoModuleManifestExternalPowerForbidden;

    output->version = input->version;
    output->minimum_api = input->minimum_api;
    output->maximum_api = input->maximum_api;
    output->address = input->address;
    output->baud = input->baud;

    if(tumomodule_string_equal(input->adapter, "bme280_i2c")) {
        if(!tumomodule_string_equal(input->bus, "i2c.external") || input->baud != 0 ||
           (input->address != 0 && input->address != 0x76 && input->address != 0x77)) {
            return TumoModuleManifestUnsafeConfiguration;
        }
        output->adapter = TumoModuleAdapterBme280I2c;
        output->bus = TumoModuleBusI2cExternal;
        output->resources = TumoModuleResourceI2cExternal | TumoModuleResourcePin15 |
                            TumoModuleResourcePin16;
    } else if(tumomodule_string_equal(input->adapter, "tumovgm_uart")) {
        if(!tumomodule_string_equal(input->bus, "uart.usart") || input->address != 0 ||
           input->baud != 230400) {
            return TumoModuleManifestUnsafeConfiguration;
        }
        output->adapter = TumoModuleAdapterTumoVgmUart;
        output->bus = TumoModuleBusUartUsart;
        output->resources = TumoModuleResourceUartUsart | TumoModuleResourceExpansion |
                            TumoModuleResourcePin13 | TumoModuleResourcePin14;
    } else {
        return TumoModuleManifestUntrustedAdapter;
    }
    return TumoModuleManifestOk;
}

const char* tumomodule_manifest_status_name(TumoModuleManifestStatus status) {
    switch(status) {
    case TumoModuleManifestOk:
        return "Ready";
    case TumoModuleManifestUnsupportedVersion:
        return "Bad version";
    case TumoModuleManifestInvalidIdentity:
        return "Bad identity";
    case TumoModuleManifestApiMismatch:
        return "API mismatch";
    case TumoModuleManifestUntrustedAdapter:
        return "Untrusted adapter";
    case TumoModuleManifestUnsafeConfiguration:
        return "Unsafe config";
    case TumoModuleManifestExternalPowerForbidden:
        return "Power forbidden";
    case TumoModuleManifestInvalidArgument:
    default:
        return "Invalid manifest";
    }
}

const char* tumomodule_bus_name(TumoModuleBus bus) {
    switch(bus) {
    case TumoModuleBusI2cExternal:
        return "I2C external";
    case TumoModuleBusUartUsart:
        return "UART USART";
    case TumoModuleBusNone:
    default:
        return "None";
    }
}

const char* tumomodule_adapter_name(TumoModuleAdapter adapter) {
    switch(adapter) {
    case TumoModuleAdapterBme280I2c:
        return "BME280";
    case TumoModuleAdapterTumoVgmUart:
        return "TumoVGM";
    case TumoModuleAdapterNone:
    default:
        return "Unknown";
    }
}

void tumomodule_resource_registry_init(TumoModuleResourceRegistry* registry) {
    if(registry != NULL) memset(registry, 0, sizeof(*registry));
}

bool tumomodule_resource_claim(
    TumoModuleResourceRegistry* registry,
    uint32_t owner,
    uint32_t resources) {
    if(registry == NULL || owner == 0 || resources == 0 || registry->owner != 0 ||
       (registry->active_resources & resources) != 0) {
        return false;
    }
    registry->owner = owner;
    registry->active_resources = resources;
    return true;
}

bool tumomodule_resource_release(TumoModuleResourceRegistry* registry, uint32_t owner) {
    if(registry == NULL || owner == 0 || registry->owner != owner) return false;
    registry->owner = 0;
    registry->active_resources = 0;
    return true;
}
