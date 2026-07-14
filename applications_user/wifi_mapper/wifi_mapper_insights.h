#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MAPPER_INSIGHTS_MAX_APS       32U
#define WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT 15U
#define WIFI_MAPPER_INSIGHTS_BSSID_SIZE    18U
#define WIFI_MAPPER_INSIGHTS_SSID_SIZE     33U

typedef enum {
    WiFiMapperSecurityOpen,
    WiFiMapperSecurityLegacy,
    WiFiMapperSecurityWpa2,
    WiFiMapperSecurityWpa3,
    WiFiMapperSecurityOther,
    WiFiMapperSecurityCount,
} WiFiMapperSecurity;

typedef struct {
    char bssids[WIFI_MAPPER_INSIGHTS_MAX_APS][WIFI_MAPPER_INSIGHTS_BSSID_SIZE];
    uint32_t observations;
    uint32_t unique_count;
    uint32_t overflow_count;
    uint32_t security_counts[WiFiMapperSecurityCount];
    uint32_t channel_counts[WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT];
    int32_t strongest_rssi;
    uint8_t strongest_channel;
    char strongest_ssid[WIFI_MAPPER_INSIGHTS_SSID_SIZE];
    char strongest_bssid[WIFI_MAPPER_INSIGHTS_BSSID_SIZE];
    bool has_strongest;
} WiFiMapperInsights;

void wifi_mapper_insights_reset(WiFiMapperInsights* insights);

WiFiMapperSecurity wifi_mapper_security_classify(const char* auth);

bool wifi_mapper_insights_observe(
    WiFiMapperInsights* insights,
    const char* bssid,
    const char* ssid,
    const char* auth,
    uint8_t channel,
    int32_t rssi);

uint8_t wifi_mapper_insights_busiest_channel(
    const WiFiMapperInsights* insights,
    uint32_t* count);
