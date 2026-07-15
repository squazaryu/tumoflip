#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MAPPER_INSPECTOR_MAX_APS    32U
#define WIFI_MAPPER_INSPECTOR_BSSID_SIZE 18U
#define WIFI_MAPPER_INSPECTOR_SSID_SIZE  33U
#define WIFI_MAPPER_INSPECTOR_AUTH_SIZE  24U

typedef struct {
    char bssid[WIFI_MAPPER_INSPECTOR_BSSID_SIZE];
    char ssid[WIFI_MAPPER_INSPECTOR_SSID_SIZE];
    char auth[WIFI_MAPPER_INSPECTOR_AUTH_SIZE];
    int32_t best_rssi;
    int32_t last_rssi;
    uint32_t samples;
    uint8_t channel;
} WiFiMapperInspectorAp;

typedef struct {
    WiFiMapperInspectorAp aps[WIFI_MAPPER_INSPECTOR_MAX_APS];
    size_t count;
    uint32_t overflow_count;
} WiFiMapperInspectorSnapshot;

typedef enum {
    WiFiMapperInspectorCategoryNew,
    WiFiMapperInspectorCategoryChanged,
    WiFiMapperInspectorCategoryGone,
    WiFiMapperInspectorCategoryCurrent,
    WiFiMapperInspectorCategoryCount,
} WiFiMapperInspectorCategory;

typedef struct {
    WiFiMapperInspectorSnapshot baseline;
    WiFiMapperInspectorSnapshot current;
    size_t new_count;
    size_t changed_count;
    size_t gone_count;
} WiFiMapperInspectorComparison;

typedef enum {
    WiFiMapperLocatorTrendWaiting,
    WiFiMapperLocatorTrendWarmer,
    WiFiMapperLocatorTrendColder,
    WiFiMapperLocatorTrendSteady,
} WiFiMapperLocatorTrend;

void wifi_mapper_inspector_snapshot_reset(WiFiMapperInspectorSnapshot* snapshot);

bool wifi_mapper_inspector_snapshot_observe(
    WiFiMapperInspectorSnapshot* snapshot,
    const char* bssid,
    const char* ssid,
    const char* auth,
    uint8_t channel,
    int32_t rssi);

void wifi_mapper_inspector_compare(WiFiMapperInspectorComparison* comparison);

size_t wifi_mapper_inspector_category_count(
    const WiFiMapperInspectorComparison* comparison,
    WiFiMapperInspectorCategory category);

const WiFiMapperInspectorAp* wifi_mapper_inspector_category_get(
    const WiFiMapperInspectorComparison* comparison,
    WiFiMapperInspectorCategory category,
    size_t index);

bool wifi_mapper_inspector_category_is_locatable(WiFiMapperInspectorCategory category);

uint8_t wifi_mapper_locator_strength_percent(int32_t rssi);

WiFiMapperLocatorTrend
    wifi_mapper_locator_trend(bool has_previous, int32_t previous_rssi, int32_t current_rssi);

const char* wifi_mapper_inspector_category_label(WiFiMapperInspectorCategory category);
const char* wifi_mapper_locator_trend_label(WiFiMapperLocatorTrend trend);
