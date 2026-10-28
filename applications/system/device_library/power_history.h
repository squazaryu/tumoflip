#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POWER_HISTORY_POINTS 128U
#define POWER_HISTORY_ESTIMATES 16U

typedef struct {
    uint32_t timestamp;
    uint32_t tick_ms;
    uint32_t heap_free;
    int32_t current_ma;
    uint16_t voltage_mv;
    int16_t temperature_tenths;
    uint8_t soc;
    uint8_t gauge_health;
    uint8_t gauge_ok;
    uint8_t charging;
    uint8_t usb;
    uint8_t ble;
} PowerHistorySample;

typedef struct {
    uint32_t version;
    PowerHistorySample points[POWER_HISTORY_POINTS];
    uint16_t head;
    uint16_t count;
    uint32_t rejected;
    uint16_t capacity_mah[POWER_HISTORY_ESTIMATES];
    uint8_t estimate_head;
    uint8_t estimate_count;
    uint32_t accepted;
    PowerHistorySample previous;
    uint64_t discharge_ma_ms;
    uint32_t duration_ms;
    uint32_t session_samples;
    uint8_t start_soc;
    uint8_t continuous;
} PowerHistory;

void power_history_init(PowerHistory* history);
bool power_history_valid(const PowerHistory* history);
bool power_history_add(PowerHistory* history, const PowerHistorySample* sample);
void power_history_pause(PowerHistory* history);
const PowerHistorySample* power_history_at(const PowerHistory* history, size_t index);
bool power_history_estimate(const PowerHistory* history, uint16_t* capacity_mah);
