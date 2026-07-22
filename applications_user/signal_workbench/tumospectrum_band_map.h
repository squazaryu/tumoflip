#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOSPECTRUM_BAND_MAP_BINS         64U
#define TUMOSPECTRUM_BAND_MAP_HISTORY_ROWS 8U

typedef struct TumoSpectrumBandMap TumoSpectrumBandMap;

typedef enum {
    TumoSpectrumBandMapRadioInternal,
    TumoSpectrumBandMapRadioExternal,
} TumoSpectrumBandMapRadio;

typedef enum {
    TumoSpectrumBandMapStatusIdle,
    TumoSpectrumBandMapStatusScanning,
    TumoSpectrumBandMapStatusHold,
    TumoSpectrumBandMapStatusBusy,
    TumoSpectrumBandMapStatusNoRadio,
    TumoSpectrumBandMapStatusError,
} TumoSpectrumBandMapStatus;

typedef struct {
    const char* name;
    uint32_t start_hz;
    uint32_t end_hz;
} TumoSpectrumBandMapBand;

typedef struct {
    TumoSpectrumBandMapStatus status;
    TumoSpectrumBandMapRadio radio;
    uint8_t band_index;
    uint8_t selected_bin;
    uint8_t zoom;
    bool external_available;
    bool hold;
    uint32_t window_start_hz;
    uint32_t window_end_hz;
    uint32_t selected_frequency_hz;
    uint32_t peak_frequency_hz;
    int8_t noise_floor_dbm;
    int8_t selected_rssi_dbm;
    int8_t peak_rssi_dbm;
    int8_t bins[TUMOSPECTRUM_BAND_MAP_BINS];
    int8_t peaks[TUMOSPECTRUM_BAND_MAP_BINS];
    int8_t history[TUMOSPECTRUM_BAND_MAP_HISTORY_ROWS][TUMOSPECTRUM_BAND_MAP_BINS];
} TumoSpectrumBandMapSnapshot;

TumoSpectrumBandMap* tumospectrum_band_map_alloc(void);
void tumospectrum_band_map_free(TumoSpectrumBandMap* map);

size_t tumospectrum_band_map_band_count(void);
const TumoSpectrumBandMapBand* tumospectrum_band_map_band(size_t index);

bool tumospectrum_band_map_start(TumoSpectrumBandMap* map, bool prefer_external);
void tumospectrum_band_map_stop(TumoSpectrumBandMap* map);
void tumospectrum_band_map_tick(TumoSpectrumBandMap* map);
bool tumospectrum_band_map_is_running(const TumoSpectrumBandMap* map);

bool tumospectrum_band_map_set_band(TumoSpectrumBandMap* map, size_t index);
bool tumospectrum_band_map_cycle_radio(TumoSpectrumBandMap* map);
void tumospectrum_band_map_move_cursor(TumoSpectrumBandMap* map, int8_t delta);
void tumospectrum_band_map_cycle_zoom(TumoSpectrumBandMap* map);
void tumospectrum_band_map_toggle_hold(TumoSpectrumBandMap* map);
void tumospectrum_band_map_snap_to_peak(TumoSpectrumBandMap* map);
void tumospectrum_band_map_get_snapshot(
    const TumoSpectrumBandMap* map,
    TumoSpectrumBandMapSnapshot* snapshot);
