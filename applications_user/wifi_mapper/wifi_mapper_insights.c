#include "wifi_mapper_insights.h"

#include <ctype.h>
#include <string.h>

static bool wifi_mapper_contains_nocase(const char* text, const char* needle) {
    if(!text || !needle || !needle[0]) {
        return false;
    }

    const size_t needle_size = strlen(needle);
    for(const char* cursor = text; *cursor; cursor++) {
        size_t index = 0U;
        while(cursor[index] && (index < needle_size) &&
              (tolower((unsigned char)cursor[index]) ==
               tolower((unsigned char)needle[index]))) {
            index++;
        }
        if(index == needle_size) {
            return true;
        }
    }

    return false;
}

static bool wifi_mapper_bssid_valid(const char* bssid) {
    if(!bssid || (strlen(bssid) != 17U)) {
        return false;
    }

    for(size_t index = 0U; index < 17U; index++) {
        if(((index + 1U) % 3U) == 0U) {
            if(bssid[index] != ':') {
                return false;
            }
        } else if(!isxdigit((unsigned char)bssid[index])) {
            return false;
        }
    }

    return true;
}

static bool wifi_mapper_bssid_seen(const WiFiMapperInsights* insights, const char* bssid) {
    for(uint32_t index = 0U; index < insights->unique_count; index++) {
        if(strcmp(insights->bssids[index], bssid) == 0) {
            return true;
        }
    }

    return false;
}

void wifi_mapper_insights_reset(WiFiMapperInsights* insights) {
    if(insights) {
        memset(insights, 0, sizeof(WiFiMapperInsights));
    }
}

WiFiMapperSecurity wifi_mapper_security_classify(const char* auth) {
    if(!auth || !auth[0]) {
        return WiFiMapperSecurityOther;
    }
    if(wifi_mapper_contains_nocase(auth, "WPA3") ||
       wifi_mapper_contains_nocase(auth, "SAE") ||
       wifi_mapper_contains_nocase(auth, "OWE")) {
        return WiFiMapperSecurityWpa3;
    }
    if(wifi_mapper_contains_nocase(auth, "WPA2") ||
       wifi_mapper_contains_nocase(auth, "RSN")) {
        return WiFiMapperSecurityWpa2;
    }
    if(wifi_mapper_contains_nocase(auth, "WEP") ||
       wifi_mapper_contains_nocase(auth, "WPA")) {
        return WiFiMapperSecurityLegacy;
    }
    if(wifi_mapper_contains_nocase(auth, "OPEN") ||
       wifi_mapper_contains_nocase(auth, "NONE") ||
       wifi_mapper_contains_nocase(auth, "OPN")) {
        return WiFiMapperSecurityOpen;
    }

    return WiFiMapperSecurityOther;
}

bool wifi_mapper_insights_observe(
    WiFiMapperInsights* insights,
    const char* bssid,
    const char* ssid,
    const char* auth,
    uint8_t channel,
    int32_t rssi) {
    if(!insights || !wifi_mapper_bssid_valid(bssid)) {
        return false;
    }

    insights->observations++;
    if(!insights->has_strongest || (rssi > insights->strongest_rssi)) {
        insights->has_strongest = true;
        insights->strongest_rssi = rssi;
        insights->strongest_channel = channel;
        if(ssid && ssid[0]) {
            strncpy(insights->strongest_ssid, ssid, sizeof(insights->strongest_ssid) - 1U);
            insights->strongest_ssid[sizeof(insights->strongest_ssid) - 1U] = '\0';
        } else {
            strncpy(
                insights->strongest_ssid,
                "<hidden>",
                sizeof(insights->strongest_ssid) - 1U);
            insights->strongest_ssid[sizeof(insights->strongest_ssid) - 1U] = '\0';
        }
        strncpy(
            insights->strongest_bssid,
            bssid,
            sizeof(insights->strongest_bssid) - 1U);
        insights->strongest_bssid[sizeof(insights->strongest_bssid) - 1U] = '\0';
    }

    if(wifi_mapper_bssid_seen(insights, bssid)) {
        return false;
    }
    if(insights->unique_count >= WIFI_MAPPER_INSIGHTS_MAX_APS) {
        insights->overflow_count++;
        return false;
    }

    strncpy(
        insights->bssids[insights->unique_count],
        bssid,
        WIFI_MAPPER_INSIGHTS_BSSID_SIZE - 1U);
    insights->bssids[insights->unique_count][WIFI_MAPPER_INSIGHTS_BSSID_SIZE - 1U] = '\0';
    insights->unique_count++;

    const WiFiMapperSecurity security = wifi_mapper_security_classify(auth);
    insights->security_counts[security]++;
    if(channel < WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT) {
        insights->channel_counts[channel]++;
    }

    return true;
}

uint8_t wifi_mapper_insights_busiest_channel(
    const WiFiMapperInsights* insights,
    uint32_t* count) {
    uint8_t busiest = 0U;
    uint32_t busiest_count = 0U;
    if(insights) {
        for(uint8_t channel = 1U; channel < WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT; channel++) {
            if(insights->channel_counts[channel] > busiest_count) {
                busiest = channel;
                busiest_count = insights->channel_counts[channel];
            }
        }
    }
    if(count) {
        *count = busiest_count;
    }
    return busiest;
}
