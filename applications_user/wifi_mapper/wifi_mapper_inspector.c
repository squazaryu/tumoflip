#include "wifi_mapper_inspector.h"

#include <ctype.h>
#include <string.h>

static void wifi_mapper_inspector_copy_lower(char* destination, size_t size, const char* source) {
    if(size == 0U) return;

    size_t index = 0U;
    if(source) {
        while(source[index] && (index + 1U < size)) {
            destination[index] = (char)tolower((unsigned char)source[index]);
            index++;
        }
    }
    destination[index] = '\0';
}

static void wifi_mapper_inspector_copy(char* destination, size_t size, const char* source) {
    if(size == 0U) return;

    size_t index = 0U;
    if(source) {
        while(source[index] && (index + 1U < size)) {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

static WiFiMapperInspectorAp*
    wifi_mapper_inspector_find_mutable(WiFiMapperInspectorSnapshot* snapshot, const char* bssid) {
    for(size_t index = 0U; index < snapshot->count; index++) {
        if(strcmp(snapshot->aps[index].bssid, bssid) == 0) {
            return &snapshot->aps[index];
        }
    }
    return NULL;
}

static const WiFiMapperInspectorAp*
    wifi_mapper_inspector_find(const WiFiMapperInspectorSnapshot* snapshot, const char* bssid) {
    for(size_t index = 0U; index < snapshot->count; index++) {
        if(strcmp(snapshot->aps[index].bssid, bssid) == 0) {
            return &snapshot->aps[index];
        }
    }
    return NULL;
}

static bool wifi_mapper_inspector_ap_changed(
    const WiFiMapperInspectorAp* baseline,
    const WiFiMapperInspectorAp* current) {
    return (baseline->channel != current->channel) ||
           (strcmp(baseline->ssid, current->ssid) != 0) ||
           (strcmp(baseline->auth, current->auth) != 0);
}

void wifi_mapper_inspector_snapshot_reset(WiFiMapperInspectorSnapshot* snapshot) {
    if(snapshot) memset(snapshot, 0, sizeof(*snapshot));
}

bool wifi_mapper_inspector_snapshot_observe(
    WiFiMapperInspectorSnapshot* snapshot,
    const char* bssid,
    const char* ssid,
    const char* auth,
    uint8_t channel,
    int32_t rssi) {
    if(!snapshot || !bssid || !bssid[0]) return false;

    char normalized_bssid[WIFI_MAPPER_INSPECTOR_BSSID_SIZE];
    wifi_mapper_inspector_copy_lower(normalized_bssid, sizeof(normalized_bssid), bssid);

    WiFiMapperInspectorAp* ap = wifi_mapper_inspector_find_mutable(snapshot, normalized_bssid);
    if(ap) {
        ap->samples++;
        ap->last_rssi = rssi;
        if(rssi > ap->best_rssi) ap->best_rssi = rssi;
        if(ssid && ssid[0]) wifi_mapper_inspector_copy(ap->ssid, sizeof(ap->ssid), ssid);
        if(auth && auth[0]) wifi_mapper_inspector_copy(ap->auth, sizeof(ap->auth), auth);
        if(channel) ap->channel = channel;
        return true;
    }

    if(snapshot->count >= WIFI_MAPPER_INSPECTOR_MAX_APS) {
        snapshot->overflow_count++;
        return false;
    }

    ap = &snapshot->aps[snapshot->count++];
    memset(ap, 0, sizeof(*ap));
    wifi_mapper_inspector_copy(ap->bssid, sizeof(ap->bssid), normalized_bssid);
    if(ssid) wifi_mapper_inspector_copy(ap->ssid, sizeof(ap->ssid), ssid);
    if(auth) wifi_mapper_inspector_copy(ap->auth, sizeof(ap->auth), auth);
    ap->channel = channel;
    ap->best_rssi = rssi;
    ap->last_rssi = rssi;
    ap->samples = 1U;
    return true;
}

void wifi_mapper_inspector_compare(WiFiMapperInspectorComparison* comparison) {
    if(!comparison) return;

    comparison->new_count = 0U;
    comparison->changed_count = 0U;
    comparison->gone_count = 0U;

    for(size_t index = 0U; index < comparison->current.count; index++) {
        const WiFiMapperInspectorAp* current = &comparison->current.aps[index];
        const WiFiMapperInspectorAp* baseline =
            wifi_mapper_inspector_find(&comparison->baseline, current->bssid);
        if(!baseline) {
            comparison->new_count++;
        } else if(wifi_mapper_inspector_ap_changed(baseline, current)) {
            comparison->changed_count++;
        }
    }

    for(size_t index = 0U; index < comparison->baseline.count; index++) {
        if(!wifi_mapper_inspector_find(
               &comparison->current, comparison->baseline.aps[index].bssid)) {
            comparison->gone_count++;
        }
    }
}

size_t wifi_mapper_inspector_category_count(
    const WiFiMapperInspectorComparison* comparison,
    WiFiMapperInspectorCategory category) {
    if(!comparison) return 0U;

    switch(category) {
    case WiFiMapperInspectorCategoryNew:
        return comparison->new_count;
    case WiFiMapperInspectorCategoryChanged:
        return comparison->changed_count;
    case WiFiMapperInspectorCategoryGone:
        return comparison->gone_count;
    case WiFiMapperInspectorCategoryCurrent:
        return comparison->current.count;
    default:
        return 0U;
    }
}

const WiFiMapperInspectorAp* wifi_mapper_inspector_category_get(
    const WiFiMapperInspectorComparison* comparison,
    WiFiMapperInspectorCategory category,
    size_t index) {
    if(!comparison) return NULL;

    size_t matched = 0U;
    if(category == WiFiMapperInspectorCategoryGone) {
        for(size_t ap_index = 0U; ap_index < comparison->baseline.count; ap_index++) {
            const WiFiMapperInspectorAp* ap = &comparison->baseline.aps[ap_index];
            if(wifi_mapper_inspector_find(&comparison->current, ap->bssid)) continue;
            if(matched++ == index) return ap;
        }
        return NULL;
    }

    for(size_t ap_index = 0U; ap_index < comparison->current.count; ap_index++) {
        const WiFiMapperInspectorAp* current = &comparison->current.aps[ap_index];
        const WiFiMapperInspectorAp* baseline =
            wifi_mapper_inspector_find(&comparison->baseline, current->bssid);
        bool include = false;
        if(category == WiFiMapperInspectorCategoryCurrent) {
            include = true;
        } else if(category == WiFiMapperInspectorCategoryNew) {
            include = baseline == NULL;
        } else if(category == WiFiMapperInspectorCategoryChanged) {
            include = baseline && wifi_mapper_inspector_ap_changed(baseline, current);
        }

        if(include && (matched++ == index)) return current;
    }

    return NULL;
}

bool wifi_mapper_inspector_category_is_locatable(WiFiMapperInspectorCategory category) {
    return category != WiFiMapperInspectorCategoryGone;
}

uint8_t wifi_mapper_locator_strength_percent(int32_t rssi) {
    if(rssi <= -100) return 0U;
    if(rssi >= -30) return 100U;
    return (uint8_t)(((rssi + 100) * 100) / 70);
}

WiFiMapperLocatorTrend
    wifi_mapper_locator_trend(bool has_previous, int32_t previous_rssi, int32_t current_rssi) {
    if(!has_previous) return WiFiMapperLocatorTrendWaiting;
    if(current_rssi >= previous_rssi + 3) return WiFiMapperLocatorTrendWarmer;
    if(current_rssi <= previous_rssi - 3) return WiFiMapperLocatorTrendColder;
    return WiFiMapperLocatorTrendSteady;
}

const char* wifi_mapper_inspector_category_label(WiFiMapperInspectorCategory category) {
    switch(category) {
    case WiFiMapperInspectorCategoryNew:
        return "New";
    case WiFiMapperInspectorCategoryChanged:
        return "Changed";
    case WiFiMapperInspectorCategoryGone:
        return "Gone";
    case WiFiMapperInspectorCategoryCurrent:
        return "Current";
    default:
        return "-";
    }
}

const char* wifi_mapper_locator_trend_label(WiFiMapperLocatorTrend trend) {
    switch(trend) {
    case WiFiMapperLocatorTrendWarmer:
        return "Warmer";
    case WiFiMapperLocatorTrendColder:
        return "Colder";
    case WiFiMapperLocatorTrendSteady:
        return "Steady";
    case WiFiMapperLocatorTrendWaiting:
    default:
        return "Waiting";
    }
}
