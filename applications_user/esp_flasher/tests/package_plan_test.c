#include "esp_flash_package_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                  \
    do {                                                                                   \
        if(!(expression)) {                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);         \
            exit(1);                                                                       \
        }                                                                                  \
    } while(0)

static const char* c5_manifest =
    "{"
    "\"schemaVersion\":1,"
    "\"packageKind\":\"tumoflip-esp32-flash-package\","
    "\"board\":{\"key\":\"esp32c5devkitc1\",\"modelId\":\"esp32-c5-devkitc-1\","
    "\"displayName\":\"ESP32-C5\",\"chipFamily\":\"esp32c5\"},"
    "\"firmware\":{\"version\":\"v1.14.1\","
    "\"sourceRepository\":\"justcallmekoko/ESP32Marauder\","
    "\"sourceRelease\":\"https://github.com/justcallmekoko/ESP32Marauder/releases/tag/v1.14.1\"},"
    "\"recipe\":{\"id\":\"c5-compat-v1\",\"status\":\"hardware-accepted\"},"
    "\"erasePolicy\":\"segments\","
    "\"segments\":["
    "{\"role\":\"bootloader\",\"fileName\":\"bootloader_0x2000.bin\",\"offset\":8192,"
    "\"size\":20464,\"sha256\":\"3e2b92a74cf406745dddc88ecb5193fd446f4b269b96d2b9991d84f41c810611\","
    "\"md5\":\"8ccd77a1fa3f67bc1c994e3519f3c03a\"},"
    "{\"role\":\"partition-table\",\"fileName\":\"partitions_0x8000.bin\",\"offset\":32768,"
    "\"size\":3072,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
    "\"md5\":\"00000000000000000000000000000000\"},"
    "{\"role\":\"application\",\"fileName\":\"marauder_0x10000.bin\",\"offset\":65536,"
    "\"size\":1024,\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\","
    "\"md5\":\"11111111111111111111111111111111\"}],"
    "\"createdAt\":\"2026-08-10T10:00:00.000Z\","
    "\"createdBy\":{\"application\":\"TumoCompanion\",\"version\":\"1.10.10\"}"
    "}";

static const char* v61_manifest =
    "{"
    "\"schemaVersion\":1,\"packageKind\":\"tumoflip-esp32-flash-package\","
    "\"board\":{\"key\":\"v6_1\",\"modelId\":\"marauder-v6-1\","
    "\"displayName\":\"Module One v6.1\",\"chipFamily\":\"esp32\"},"
    "\"firmware\":{\"version\":\"v1.14.1\","
    "\"sourceRepository\":\"justcallmekoko/ESP32Marauder\","
    "\"sourceRelease\":\"https://github.com/justcallmekoko/ESP32Marauder/releases/tag/v1.14.1\"},"
    "\"recipe\":{\"id\":\"upstream-factory-v1\",\"status\":\"authoritative\"},"
    "\"erasePolicy\":\"segments\",\"segments\":["
    "{\"role\":\"bootloader\",\"fileName\":\"bootloader_0x1000.bin\",\"offset\":4096,\"size\":4096,"
    "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"md5\":\"00000000000000000000000000000000\"},"
    "{\"role\":\"partition-table\",\"fileName\":\"partitions_0x8000.bin\",\"offset\":32768,\"size\":3072,"
    "\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\",\"md5\":\"11111111111111111111111111111111\"},"
    "{\"role\":\"ota-data\",\"fileName\":\"boot_app0_0xe000.bin\",\"offset\":57344,\"size\":8192,"
    "\"sha256\":\"2222222222222222222222222222222222222222222222222222222222222222\",\"md5\":\"22222222222222222222222222222222\"},"
    "{\"role\":\"application\",\"fileName\":\"marauder_0x10000.bin\",\"offset\":65536,\"size\":1024,"
    "\"sha256\":\"3333333333333333333333333333333333333333333333333333333333333333\",\"md5\":\"33333333333333333333333333333333\"}],"
    "\"createdAt\":\"2026-08-10T10:00:00Z\","
    "\"createdBy\":{\"application\":\"TumoCompanion\",\"version\":\"1.10.10\"}}";

static char* replace_once(const char* source, const char* from, const char* to) {
    const char* location = strstr(source, from);
    CHECK(location != NULL);
    const size_t size = strlen(source) - strlen(from) + strlen(to) + 1U;
    char* output = malloc(size);
    CHECK(output != NULL);
    const size_t prefix = (size_t)(location - source);
    memcpy(output, source, prefix);
    memcpy(output + prefix, to, strlen(to));
    strcpy(output + prefix + strlen(to), location + strlen(from));
    return output;
}

static bool parse(const char* json, EspFlashPackagePlan* plan) {
    char error[128] = {0};
    const bool result =
        esp_flash_package_parse_manifest(json, strlen(json), plan, error, sizeof(error));
    if(!result) fprintf(stderr, "expected rejection detail: %s\n", error);
    return result;
}

int main(void) {
    EspFlashPackagePlan plan;
    CHECK(parse(c5_manifest, &plan));
    CHECK(plan.target == EspFlashPackageTargetEsp32C5);
    CHECK(plan.segment_count == 3U);
    CHECK(plan.total_size == 24560U);
    const char* c5_names[] = {
        "marauder_0x10000.bin", "bootloader_0x2000.bin", "partitions_0x8000.bin"};
    CHECK(esp_flash_package_validate_bin_names(&plan, c5_names, 3U));
    const char* c5_extra[] = {
        "marauder_0x10000.bin",
        "bootloader_0x2000.bin",
        "partitions_0x8000.bin",
        "unexpected.bin"};
    CHECK(!esp_flash_package_validate_bin_names(&plan, c5_extra, 4U));

    CHECK(parse(v61_manifest, &plan));
    CHECK(plan.target == EspFlashPackageTargetEsp32);
    CHECK(plan.segment_count == 4U);

    char* changed = replace_once(c5_manifest, "8ccd77", "8CCD77");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(c5_manifest, "bootloader_0x2000.bin", "../bootloader.bin");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(c5_manifest, "3e2b92a7", "4e2b92a7");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(c5_manifest, "\"size\":20464", "\"size\":20463");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(c5_manifest, "\"segments\":[", "\"segments\":[] ,\"unused\":[");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"key\":\"v6_1\"", "\"key\":\"unknown\"");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "\"id\":\"upstream-factory-v1\"",
        "\"id\":\"unknown-recipe\"");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"size\":3072", "\"size\":40000");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"role\":\"ota-data\"", "\"role\":\"partition-table\"");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "boot_app0_0xe000.bin", "partitions_0x8000.bin");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"offset\":57344", "\"offset\":32768");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "}],\"createdAt\"",
        "},{\"role\":\"application\",\"fileName\":\"extra.bin\",\"offset\":131072,"
        "\"size\":1024,\"sha256\":\"4444444444444444444444444444444444444444444444444444444444444444\","
        "\"md5\":\"44444444444444444444444444444444\"}],\"createdAt\"");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "releases/tag/v1.14.1",
        "releases/tag/v1.14.0");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "2026-08-10T10:00:00Z",
        "2026/08/10 10:00:00");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "2026-08-10T10:00:00Z",
        "2026-08-10T10:00:00.Z");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "2026-08-10T10:00:00Z",
        "2026-02-30T10:00:00Z");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"size\":1024", "\"size\":4294967296");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"size\":1024", "\"size\":1e3");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(v61_manifest, "\"erasePolicy\":\"segments\"", "\"erasePolicy\":\"chip\"");
    CHECK(!parse(changed, &plan));
    free(changed);

    changed = replace_once(
        v61_manifest,
        "justcallmekoko/ESP32Marauder",
        "untrusted/example");
    CHECK(!parse(changed, &plan));
    free(changed);

    CHECK(esp_flash_package_directory_name_allowed("module_one_v6_1_v1_14_1_manual"));
    CHECK(!esp_flash_package_directory_name_allowed("_archive"));
    CHECK(!esp_flash_package_directory_name_allowed(".partial-test_manual"));
    CHECK(!esp_flash_package_directory_name_allowed("bad name_manual"));
    CHECK(!esp_flash_package_directory_name_allowed("replacement.partial-x_manual"));

    char* oversized = malloc(16386U);
    CHECK(oversized != NULL);
    memset(oversized, ' ', 16385U);
    oversized[16385U] = '\0';
    CHECK(!esp_flash_package_parse_manifest(
        oversized, 16385U, &plan, NULL, 0U));
    free(oversized);

    puts("package plan tests: OK");
    return 0;
}
