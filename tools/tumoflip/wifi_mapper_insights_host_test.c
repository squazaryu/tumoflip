#include "wifi_mapper_insights.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_security_classification(void) {
    assert(wifi_mapper_security_classify("OPEN") == WiFiMapperSecurityOpen);
    assert(wifi_mapper_security_classify("WEP") == WiFiMapperSecurityLegacy);
    assert(wifi_mapper_security_classify("WPA-PSK") == WiFiMapperSecurityLegacy);
    assert(wifi_mapper_security_classify("WPA2-PSK CCMP") == WiFiMapperSecurityWpa2);
    assert(wifi_mapper_security_classify("WPA3 SAE") == WiFiMapperSecurityWpa3);
    assert(wifi_mapper_security_classify("OWE") == WiFiMapperSecurityWpa3);
    assert(wifi_mapper_security_classify("") == WiFiMapperSecurityOther);
}

static void test_unique_and_strongest_tracking(void) {
    WiFiMapperInsights insights;
    wifi_mapper_insights_reset(&insights);

    assert(wifi_mapper_insights_observe(
        &insights, "AA:BB:CC:DD:EE:01", "Lab", "WPA2", 6U, -70));
    assert(!wifi_mapper_insights_observe(
        &insights, "AA:BB:CC:DD:EE:01", "Lab", "WPA2", 6U, -40));
    assert(wifi_mapper_insights_observe(
        &insights, "AA:BB:CC:DD:EE:02", "Guest", "OPEN", 11U, -55));

    assert(insights.observations == 3U);
    assert(insights.unique_count == 2U);
    assert(insights.security_counts[WiFiMapperSecurityOpen] == 1U);
    assert(insights.security_counts[WiFiMapperSecurityWpa2] == 1U);
    assert(insights.channel_counts[6] == 1U);
    assert(insights.channel_counts[11] == 1U);
    assert(insights.strongest_rssi == -40);
    assert(insights.strongest_channel == 6U);
    assert(strcmp(insights.strongest_ssid, "Lab") == 0);
}

static void test_capacity_is_bounded(void) {
    WiFiMapperInsights insights;
    wifi_mapper_insights_reset(&insights);

    char bssid[WIFI_MAPPER_INSIGHTS_BSSID_SIZE];
    for(uint32_t index = 0U; index < WIFI_MAPPER_INSIGHTS_MAX_APS + 3U; index++) {
        snprintf(bssid, sizeof(bssid), "02:00:00:00:00:%02X", (unsigned)index);
        wifi_mapper_insights_observe(&insights, bssid, "AP", "WPA2", 1U, -60);
    }

    assert(insights.unique_count == WIFI_MAPPER_INSIGHTS_MAX_APS);
    assert(insights.overflow_count == 3U);
}

static void test_busiest_channel(void) {
    WiFiMapperInsights insights;
    wifi_mapper_insights_reset(&insights);
    wifi_mapper_insights_observe(
        &insights, "02:00:00:00:00:01", "One", "WPA2", 1U, -60);
    wifi_mapper_insights_observe(
        &insights, "02:00:00:00:00:02", "Two", "WPA2", 6U, -50);
    wifi_mapper_insights_observe(
        &insights, "02:00:00:00:00:03", "Three", "OPEN", 6U, -40);

    uint32_t count = 0U;
    assert(wifi_mapper_insights_busiest_channel(&insights, &count) == 6U);
    assert(count == 2U);
}

int main(void) {
    test_security_classification();
    test_unique_and_strongest_tracking();
    test_capacity_is_bounded();
    test_busiest_channel();
    puts("wifi_mapper_insights_host_test: PASS");
    return 0;
}
