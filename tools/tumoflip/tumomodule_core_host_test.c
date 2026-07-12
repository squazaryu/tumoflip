#include "../../applications_user/tumomodule_runtime/tumomodule_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if(!(condition)) {                                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while(false)

static TumoModuleManifestInput valid_bme280(void) {
    return (TumoModuleManifestInput){
        .version = 1,
        .id = "module_one_bme280",
        .name = "Module One BME280",
        .adapter = "bme280_i2c",
        .bus = "i2c.external",
        .minimum_api = 88,
        .maximum_api = 88,
    };
}

static TumoModuleManifestInput valid_tumovgm(void) {
    return (TumoModuleManifestInput){
        .version = 1,
        .id = "tumovgm_rp2040",
        .name = "TumoVGM RP2040",
        .adapter = "tumovgm_uart",
        .bus = "uart.usart",
        .minimum_api = 88,
        .maximum_api = 88,
        .baud = 230400,
    };
}

static bool test_valid_manifests(void) {
    TumoModuleManifest output;
    TumoModuleManifestInput input = valid_bme280();
    CHECK(tumomodule_manifest_build(&input, 88, &output) == TumoModuleManifestOk);
    CHECK(output.adapter == TumoModuleAdapterBme280I2c);
    CHECK(output.bus == TumoModuleBusI2cExternal);
    CHECK(output.resources & TumoModuleResourceI2cExternal);
    CHECK(output.resources & TumoModuleResourcePin15);
    CHECK(output.resources & TumoModuleResourcePin16);

    input = valid_tumovgm();
    CHECK(tumomodule_manifest_build(&input, 88, &output) == TumoModuleManifestOk);
    CHECK(output.adapter == TumoModuleAdapterTumoVgmUart);
    CHECK(output.bus == TumoModuleBusUartUsart);
    CHECK(output.resources & TumoModuleResourceExpansion);
    CHECK(output.resources & TumoModuleResourcePin13);
    CHECK(output.resources & TumoModuleResourcePin14);
    return true;
}

static bool test_manifests_fail_closed(void) {
    TumoModuleManifest output;
    TumoModuleManifestInput input = valid_bme280();

    input.version = 2;
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) ==
        TumoModuleManifestUnsupportedVersion);
    input = valid_bme280();
    input.id = "Bad ID";
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) == TumoModuleManifestInvalidIdentity);
    input = valid_bme280();
    input.maximum_api = 87;
    CHECK(tumomodule_manifest_build(&input, 88, &output) == TumoModuleManifestApiMismatch);
    input = valid_bme280();
    input.adapter = "unknown_native_fal";
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) ==
        TumoModuleManifestUntrustedAdapter);
    input = valid_bme280();
    input.external_power = true;
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) ==
        TumoModuleManifestExternalPowerForbidden);
    input = valid_bme280();
    input.address = 0x55;
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) ==
        TumoModuleManifestUnsafeConfiguration);
    input = valid_tumovgm();
    input.baud = 115200;
    CHECK(
        tumomodule_manifest_build(&input, 88, &output) ==
        TumoModuleManifestUnsafeConfiguration);
    return true;
}

static bool test_resource_registry(void) {
    TumoModuleResourceRegistry registry;
    tumomodule_resource_registry_init(&registry);
    CHECK(tumomodule_resource_claim(&registry, 1, TumoModuleResourceUartUsart));
    CHECK(!tumomodule_resource_claim(&registry, 2, TumoModuleResourceI2cExternal));
    CHECK(!tumomodule_resource_release(&registry, 2));
    CHECK(tumomodule_resource_release(&registry, 1));
    CHECK(tumomodule_resource_claim(&registry, 2, TumoModuleResourceI2cExternal));
    CHECK(tumomodule_resource_release(&registry, 2));
    return true;
}

static bool test_deterministic_manifest_corpus(void) {
    uint32_t state = UINT32_C(0x65C0FFEE);
    char id[TumoModuleIdSize + 8];
    char name[TumoModuleNameSize + 8];
    char adapter[TumoModuleAdapterNameSize + 8];
    char bus[TumoModuleAdapterNameSize + 8];
    TumoModuleManifest output;

    for(size_t iteration = 0; iteration < 5000; iteration++) {
        char* buffers[] = {id, name, adapter, bus};
        const size_t sizes[] = {sizeof(id), sizeof(name), sizeof(adapter), sizeof(bus)};
        for(size_t buffer_index = 0; buffer_index < 4; buffer_index++) {
            for(size_t index = 0; index + 1 < sizes[buffer_index]; index++) {
                state = state * UINT32_C(1664525) + UINT32_C(1013904223);
                buffers[buffer_index][index] = (char)(state >> 24);
            }
            buffers[buffer_index][sizes[buffer_index] - 1] = '\0';
        }
        const TumoModuleManifestInput input = {
            .version = state & 3U,
            .id = id,
            .name = name,
            .adapter = adapter,
            .bus = bus,
            .minimum_api = state & 0xFFU,
            .maximum_api = (state >> 8) & 0xFFU,
            .address = (state >> 16) & 0xFFU,
            .baud = state,
            .external_power = (state & 1U) != 0,
        };
        tumomodule_manifest_build(&input, 88, &output);
    }
    return true;
}

int main(void) {
    if(!test_valid_manifests() || !test_manifests_fail_closed() ||
       !test_resource_registry() || !test_deterministic_manifest_corpus()) {
        return 1;
    }
    puts("tumomodule_core_host_test: PASS");
    return 0;
}
