#include "power_history.h"

#include <string.h>

static bool power_sample_valid(const PowerHistorySample* sample) {
    return sample && sample->gauge_ok == 1U && sample->charging <= 1U &&
           sample->usb <= 1U && sample->ble <= 1U && sample->soc <= 100U &&
           sample->gauge_health <= 100U && sample->voltage_mv >= 2500U &&
           sample->voltage_mv <= 4500U && sample->current_ma >= -5000 &&
           sample->current_ma <= 5000 && sample->temperature_tenths >= -200 &&
           sample->temperature_tenths <= 600;
}

void power_history_pause(PowerHistory* history) {
    if(!history) return;
    history->continuous = 0;
    history->duration_ms = 0;
    history->discharge_ma_ms = 0;
    history->session_samples = 0;
    history->start_soc = 0;
}

void power_history_init(PowerHistory* history) {
    if(!history) return;
    memset(history, 0, sizeof(*history));
    history->version = 1U;
}

bool power_history_valid(const PowerHistory* history) {
    if(!history || history->version != 1U || history->head >= POWER_HISTORY_POINTS ||
       history->count > POWER_HISTORY_POINTS || history->estimate_head >= POWER_HISTORY_ESTIMATES ||
       history->estimate_count > POWER_HISTORY_ESTIMATES || history->continuous > 1U ||
       history->accepted < history->estimate_count || history->duration_ms > 86400000U ||
       history->discharge_ma_ms > 432000000000ULL || history->start_soc > 100U) {
        return false;
    }
    for(unsigned i = 0; i < history->count; i++) {
        const unsigned index =
            (history->head + POWER_HISTORY_POINTS - history->count + i) % POWER_HISTORY_POINTS;
        if(!power_sample_valid(&history->points[index])) return false;
    }
    for(unsigned i = 0; i < history->estimate_count; i++) {
        if(history->capacity_mah[i] < 100U || history->capacity_mah[i] > 5000U) return false;
    }
    return !history->continuous || power_sample_valid(&history->previous);
}

const PowerHistorySample* power_history_at(const PowerHistory* history, size_t index) {
    if(!history || history->head >= POWER_HISTORY_POINTS || history->count > POWER_HISTORY_POINTS ||
       index >= history->count) {
        return NULL;
    }
    return &history->points[(history->head + POWER_HISTORY_POINTS - history->count + index) %
                            POWER_HISTORY_POINTS];
}

static uint16_t power_capacity_median(const PowerHistory* history) {
    uint16_t values[POWER_HISTORY_ESTIMATES];
    for(unsigned i = 0; i < history->estimate_count; i++) {
        values[i] = history->capacity_mah[i];
        for(unsigned j = i; j > 0 && values[j] < values[j - 1]; j--) {
            const uint16_t value = values[j];
            values[j] = values[j - 1];
            values[j - 1] = value;
        }
    }
    const unsigned n = history->estimate_count;
    return n & 1U ? values[n / 2U] : (values[n / 2U - 1U] + values[n / 2U]) / 2U;
}

bool power_history_estimate(const PowerHistory* history, uint16_t* capacity_mah) {
    if(!capacity_mah || !power_history_valid(history) || history->estimate_count < 3U) return false;
    *capacity_mah = power_capacity_median(history);
    return true;
}

static void power_discharge_begin(PowerHistory* history, const PowerHistorySample* sample) {
    power_history_pause(history);
    history->continuous = 1;
    history->start_soc = sample->soc;
    history->previous = *sample;
    history->session_samples = 1;
}

static void power_reject(PowerHistory* history) {
    if(history->rejected != UINT32_MAX) history->rejected++;
}

static void power_capacity_accept(PowerHistory* history, uint16_t capacity) {
    if(history->estimate_count >= 3U) {
        const uint16_t median = power_capacity_median(history);
        const uint32_t difference = capacity > median ? capacity - median : median - capacity;
        if(difference * 100U > (uint32_t)median * 15U) {
            power_reject(history);
            return;
        }
    }
    history->capacity_mah[history->estimate_head] = capacity;
    history->estimate_head = (history->estimate_head + 1U) % POWER_HISTORY_ESTIMATES;
    if(history->estimate_count < POWER_HISTORY_ESTIMATES) history->estimate_count++;
    if(history->accepted != UINT32_MAX) history->accepted++;
}

bool power_history_add(PowerHistory* history, const PowerHistorySample* sample) {
    if(!power_history_valid(history)) return false;
    if(!power_sample_valid(sample)) {
        power_history_pause(history);
        power_reject(history);
        return false;
    }
    const PowerHistorySample value = *sample;
    history->points[history->head] = value;
    history->head = (history->head + 1U) % POWER_HISTORY_POINTS;
    if(history->count < POWER_HISTORY_POINTS) history->count++;
    if(value.charging || value.usb || value.current_ma > 0 ||
       value.temperature_tenths < 0 || value.temperature_tenths > 450) {
        power_history_pause(history);
        return true;
    }
    if(!history->continuous) {
        power_discharge_begin(history, &value);
        return true;
    }
    const uint32_t elapsed = value.tick_ms - history->previous.tick_ms;
    if(!elapsed || elapsed > 10000U || value.soc > history->previous.soc ||
       history->duration_ms > 86400000U - elapsed) {
        power_discharge_begin(history, &value);
        return true;
    }
    const int64_t sum_ma = (int64_t)value.current_ma + history->previous.current_ma;
    history->discharge_ma_ms += (uint64_t)(-sum_ma * elapsed / 2);
    history->duration_ms += elapsed;
    history->session_samples++;
    history->previous = value;
    const unsigned drop = history->start_soc - value.soc;
    if(drop >= 15U) {
        if(history->duration_ms >= 300000U && history->session_samples >= 30U) {
            const uint64_t candidate = history->discharge_ma_ms * 100U / (3600000ULL * drop);
            if(candidate >= 100U && candidate <= 5000U) {
                power_capacity_accept(history, (uint16_t)candidate);
            } else {
                power_reject(history);
            }
        }
        power_discharge_begin(history, &value);
    }
    return true;
}
