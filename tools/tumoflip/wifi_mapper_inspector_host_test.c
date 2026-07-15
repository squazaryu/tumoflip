#include "wifi_mapper_inspector.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void observe(
    WiFiMapperInspectorSnapshot* snapshot,
    const char* bssid,
    const char* ssid,
    const char* auth,
    uint8_t channel,
    int32_t rssi) {
    assert(wifi_mapper_inspector_snapshot_observe(snapshot, bssid, ssid, auth, channel, rssi));
}

static void load_snapshot(const char* path, WiFiMapperInspectorSnapshot* snapshot) {
    FILE* file = fopen(path, "r");
    assert(file);

    char line[256];
    while(fgets(line, sizeof(line), file)) {
        if(strncmp(line, "tick_ms,", 8) == 0) continue;

        char type[16];
        char bssid[WIFI_MAPPER_INSPECTOR_BSSID_SIZE];
        char ssid[WIFI_MAPPER_INSPECTOR_SSID_SIZE];
        char auth[WIFI_MAPPER_INSPECTOR_AUTH_SIZE];
        int rssi = 0;
        unsigned int channel = 0U;
        const int fields = sscanf(
            line,
            "%*u,%15[^,],%d,%u,%17[^,],%32[^,],%23[^,]",
            type,
            &rssi,
            &channel,
            bssid,
            ssid,
            auth);
        assert(fields == 6);
        assert(strcmp(type, "ap") == 0);
        assert(channel <= UINT8_MAX);
        observe(snapshot, bssid, ssid, auth, (uint8_t)channel, rssi);
    }

    assert(fclose(file) == 0);
}

int main(int argc, char** argv) {
    assert(argc == 3);

    WiFiMapperInspectorComparison comparison;
    memset(&comparison, 0, sizeof(comparison));

    load_snapshot(argv[1], &comparison.baseline);
    load_snapshot(argv[2], &comparison.current);

    wifi_mapper_inspector_compare(&comparison);
    assert(comparison.new_count == 1U);
    assert(comparison.changed_count == 1U);
    assert(comparison.gone_count == 1U);
    assert(comparison.current.count == 3U);

    const WiFiMapperInspectorAp* stable =
        wifi_mapper_inspector_category_get(&comparison, WiFiMapperInspectorCategoryCurrent, 0U);
    assert(stable);
    assert(strcmp(stable->bssid, "aa:00:00:00:00:01") == 0);
    assert(stable->samples == 2U);
    assert(stable->best_rssi == -55);
    assert(stable->last_rssi == -55);

    const WiFiMapperInspectorAp* added =
        wifi_mapper_inspector_category_get(&comparison, WiFiMapperInspectorCategoryNew, 0U);
    assert(added && strcmp(added->ssid, "New") == 0);
    assert(
        wifi_mapper_inspector_category_get(&comparison, WiFiMapperInspectorCategoryNew, 1U) ==
        NULL);

    const WiFiMapperInspectorAp* changed =
        wifi_mapper_inspector_category_get(&comparison, WiFiMapperInspectorCategoryChanged, 0U);
    assert(changed && strcmp(changed->auth, "WPA3") == 0);

    const WiFiMapperInspectorAp* gone =
        wifi_mapper_inspector_category_get(&comparison, WiFiMapperInspectorCategoryGone, 0U);
    assert(gone && strcmp(gone->ssid, "Gone") == 0);
    assert(!wifi_mapper_inspector_category_is_locatable(WiFiMapperInspectorCategoryGone));
    assert(wifi_mapper_inspector_category_is_locatable(WiFiMapperInspectorCategoryCurrent));

    assert(wifi_mapper_locator_strength_percent(-120) == 0U);
    assert(wifi_mapper_locator_strength_percent(-30) == 100U);
    assert(wifi_mapper_locator_strength_percent(-65) == 50U);
    assert(wifi_mapper_locator_trend(false, -70, -60) == WiFiMapperLocatorTrendWaiting);
    assert(wifi_mapper_locator_trend(true, -70, -66) == WiFiMapperLocatorTrendWarmer);
    assert(wifi_mapper_locator_trend(true, -60, -64) == WiFiMapperLocatorTrendColder);
    assert(wifi_mapper_locator_trend(true, -60, -58) == WiFiMapperLocatorTrendSteady);

    WiFiMapperInspectorSnapshot full;
    wifi_mapper_inspector_snapshot_reset(&full);
    char bssid[WIFI_MAPPER_INSPECTOR_BSSID_SIZE];
    for(size_t index = 0U; index < WIFI_MAPPER_INSPECTOR_MAX_APS; index++) {
        snprintf(bssid, sizeof(bssid), "00:00:00:00:00:%02x", (unsigned int)index);
        observe(&full, bssid, "Bounded", "WPA2", 1, -70);
    }
    assert(!wifi_mapper_inspector_snapshot_observe(
        &full, "ff:ff:ff:ff:ff:ff", "Overflow", "OPEN", 1, -80));
    assert(full.count == WIFI_MAPPER_INSPECTOR_MAX_APS);
    assert(full.overflow_count == 1U);

    puts("wifi_mapper_inspector_host_test: PASS");
    return 0;
}
