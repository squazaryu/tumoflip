#include "tumospectrum_band_map.h"

#include <furi.h>
#include <lib/subghz/devices/devices.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

#include <stdlib.h>
#include <string.h>

#define TAG "TumoSpectrumBandMap"

#define TUMOSPECTRUM_BAND_MAP_OWNER           "tumospectrum_map"
#define TUMOSPECTRUM_BAND_MAP_ACQUIRE_TIMEOUT 500U
#define TUMOSPECTRUM_BAND_MAP_SAMPLE_DELAY_MS 1U
#define TUMOSPECTRUM_BAND_MAP_BINS_PER_TICK   4U
#define TUMOSPECTRUM_BAND_MAP_INTERNAL_NAME   "cc1101_int"
#define TUMOSPECTRUM_BAND_MAP_EXTERNAL_NAME   "cc1101_ext"
#define TUMOSPECTRUM_BAND_MAP_RSSI_MIN        (-127)
#define TUMOSPECTRUM_BAND_MAP_ZOOM_MAX        4U

static const TumoSpectrumBandMapBand tumospectrum_band_map_bands[] = {
    {.name = "315", .start_hz = 314000000U, .end_hz = 316000000U},
    {.name = "390", .start_hz = 389000000U, .end_hz = 391000000U},
    {.name = "433", .start_hz = 433050000U, .end_hz = 434790000U},
    {.name = "868", .start_hz = 863000000U, .end_hz = 870000000U},
    {.name = "915", .start_hz = 902000000U, .end_hz = 928000000U},
};

struct TumoSpectrumBandMap {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* device;
    const SubGhzDevice* second_device;
    TumoSpectrumBandMapSnapshot snapshot;
    uint8_t scan_bin;
    bool devices_initialized;
    bool device_begun;
    bool second_begun;
};

static int8_t tumospectrum_band_map_clamp_rssi(float value) {
    if(value != value) return TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    if(value < (float)TUMOSPECTRUM_BAND_MAP_RSSI_MIN) {
        return TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    }
    if(value > -1.0f) return -1;
    return (int8_t)value;
}

static uint32_t tumospectrum_band_map_frequency_for_bin(
    const TumoSpectrumBandMapSnapshot* snapshot,
    uint8_t bin) {
    const uint64_t span = snapshot->window_end_hz - snapshot->window_start_hz;
    return snapshot->window_start_hz +
           (uint32_t)((span * bin) / (TUMOSPECTRUM_BAND_MAP_BINS - 1U));
}

static uint8_t tumospectrum_band_map_bin_for_frequency(
    const TumoSpectrumBandMapSnapshot* snapshot,
    uint32_t frequency) {
    if(frequency <= snapshot->window_start_hz) return 0U;
    if(frequency >= snapshot->window_end_hz) return TUMOSPECTRUM_BAND_MAP_BINS - 1U;
    const uint64_t span = snapshot->window_end_hz - snapshot->window_start_hz;
    const uint64_t offset = frequency - snapshot->window_start_hz;
    return (uint8_t)((offset * (TUMOSPECTRUM_BAND_MAP_BINS - 1U) + span / 2U) / span);
}

static void tumospectrum_band_map_reset_samples(TumoSpectrumBandMap* map) {
    memset(map->snapshot.bins, TUMOSPECTRUM_BAND_MAP_RSSI_MIN, sizeof(map->snapshot.bins));
    memset(map->snapshot.peaks, TUMOSPECTRUM_BAND_MAP_RSSI_MIN, sizeof(map->snapshot.peaks));
    memset(map->snapshot.history, TUMOSPECTRUM_BAND_MAP_RSSI_MIN, sizeof(map->snapshot.history));
    map->snapshot.noise_floor_dbm = TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    map->snapshot.selected_rssi_dbm = TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    map->snapshot.peak_rssi_dbm = TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    map->snapshot.peak_frequency_hz = 0U;
    map->scan_bin = 0U;
}

static void tumospectrum_band_map_reset_window(TumoSpectrumBandMap* map) {
    const TumoSpectrumBandMapBand* band = &tumospectrum_band_map_bands[map->snapshot.band_index];
    map->snapshot.window_start_hz = band->start_hz;
    map->snapshot.window_end_hz = band->end_hz;
    map->snapshot.zoom = 1U;
    map->snapshot.selected_bin = TUMOSPECTRUM_BAND_MAP_BINS / 2U;
    map->snapshot.selected_frequency_hz =
        tumospectrum_band_map_frequency_for_bin(&map->snapshot, map->snapshot.selected_bin);
    tumospectrum_band_map_reset_samples(map);
}

static void tumospectrum_band_map_finish_sweep(TumoSpectrumBandMap* map) {
    int8_t sorted[TUMOSPECTRUM_BAND_MAP_BINS];
    memcpy(sorted, map->snapshot.bins, sizeof(sorted));
    for(size_t index = 1U; index < TUMOSPECTRUM_BAND_MAP_BINS; index++) {
        const int8_t candidate = sorted[index];
        size_t position = index;
        while(position > 0U && sorted[position - 1U] > candidate) {
            sorted[position] = sorted[position - 1U];
            position--;
        }
        sorted[position] = candidate;
    }
    map->snapshot.noise_floor_dbm = sorted[TUMOSPECTRUM_BAND_MAP_BINS / 4U];

    memmove(
        map->snapshot.history[1],
        map->snapshot.history[0],
        sizeof(map->snapshot.history) - sizeof(map->snapshot.history[0]));
    memcpy(map->snapshot.history[0], map->snapshot.bins, sizeof(map->snapshot.bins));

    map->snapshot.peak_rssi_dbm = TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
    map->snapshot.peak_frequency_hz = 0U;
    for(uint8_t bin = 0U; bin < TUMOSPECTRUM_BAND_MAP_BINS; bin++) {
        const int8_t decayed = map->snapshot.peaks[bin] > TUMOSPECTRUM_BAND_MAP_RSSI_MIN ?
                                   map->snapshot.peaks[bin] - 1 :
                                   TUMOSPECTRUM_BAND_MAP_RSSI_MIN;
        map->snapshot.peaks[bin] = map->snapshot.bins[bin] > decayed ? map->snapshot.bins[bin] :
                                                                       decayed;
        if(map->snapshot.peaks[bin] > map->snapshot.peak_rssi_dbm) {
            map->snapshot.peak_rssi_dbm = map->snapshot.peaks[bin];
            map->snapshot.peak_frequency_hz =
                tumospectrum_band_map_frequency_for_bin(&map->snapshot, bin);
        }
    }
    map->snapshot.selected_rssi_dbm = map->snapshot.bins[map->snapshot.selected_bin];
    // Only distinct peaks measurably above this sweep's noise enter the bounded
    // session history. This is RSSI evidence, never protocol identification.
    if(map->snapshot.peak_rssi_dbm > -95 &&
       map->snapshot.peak_rssi_dbm - map->snapshot.noise_floor_dbm >= 10) {
        unsigned target = map->snapshot.peak_count;
        for(unsigned i = 0; i < map->snapshot.peak_count; i++) {
            uint32_t f = map->snapshot.observed_peaks[i].frequency_hz;
            uint32_t p = map->snapshot.peak_frequency_hz;
            if((f > p ? f - p : p - f) <= 200000U) { target = i; break; }
        }
        if(target == 16) {
            unsigned weakest = 0;
            for(unsigned i = 1; i < 16; i++)
                if(map->snapshot.observed_peaks[i].rssi_dbm < map->snapshot.observed_peaks[weakest].rssi_dbm) weakest = i;
            if(map->snapshot.observed_peaks[weakest].rssi_dbm >= map->snapshot.peak_rssi_dbm) return;
            target = weakest;
            map->snapshot.observed_peaks[target].observations = 0;
        } else if(target == map->snapshot.peak_count) {
            map->snapshot.peak_count++;
            map->snapshot.observed_peaks[target].observations = 0;
        }
        TumoSpectrumBandPeak* peak = &map->snapshot.observed_peaks[target];
        if(!peak->observations || peak->rssi_dbm < map->snapshot.peak_rssi_dbm) {
            peak->frequency_hz = map->snapshot.peak_frequency_hz;
            peak->rssi_dbm = map->snapshot.peak_rssi_dbm;
            peak->radio = map->snapshot.radio;
        }
        if(peak->observations < UINT16_MAX) peak->observations++;
    }
}

static bool tumospectrum_band_map_begin_device(TumoSpectrumBandMap* map, TumoSpectrumBandMapRadio requested) {
    map->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!subghz_radio_broker_acquire(
           map->broker,
           TUMOSPECTRUM_BAND_MAP_OWNER,
           TUMOSPECTRUM_BAND_MAP_ACQUIRE_TIMEOUT,
           &map->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        map->broker = NULL;
        map->snapshot.status = TumoSpectrumBandMapStatusBusy;
        return false;
    }

    subghz_devices_init();
    map->devices_initialized = true;
    subghz_radio_broker_set_state(map->broker, &map->lease, SubGhzRadioBrokerStateProbing);

    const SubGhzDevice* external = subghz_devices_get_by_name(TUMOSPECTRUM_BAND_MAP_EXTERNAL_NAME);
    map->snapshot.external_available = false;
    if(external && subghz_radio_broker_external_power_on(map->broker, &map->lease)) {
        map->snapshot.external_available = subghz_devices_is_connect(external);
    }

    if(requested == TumoSpectrumBandMapRadioExternal && map->snapshot.external_available) {
        map->device = external;
        map->snapshot.radio = TumoSpectrumBandMapRadioExternal;
    } else {
        if(requested != TumoSpectrumBandMapRadioDual || !map->snapshot.external_available)
            subghz_radio_broker_external_power_off(map->broker, &map->lease);
        map->device = subghz_devices_get_by_name(TUMOSPECTRUM_BAND_MAP_INTERNAL_NAME);
        map->snapshot.radio = TumoSpectrumBandMapRadioInternal;
    }

    bool device_ready = map->device != NULL;
    if(device_ready && map->snapshot.radio == TumoSpectrumBandMapRadioExternal) {
        device_ready = subghz_devices_begin(map->device);
    }
    if(!device_ready && map->snapshot.radio == TumoSpectrumBandMapRadioExternal) {
        // The external driver allocates its context before reporting init failure.
        subghz_devices_end(map->device);
        subghz_radio_broker_external_power_off(map->broker, &map->lease);
        map->device = subghz_devices_get_by_name(TUMOSPECTRUM_BAND_MAP_INTERNAL_NAME);
        map->snapshot.radio = TumoSpectrumBandMapRadioInternal;
        device_ready = map->device != NULL;
    }

    if(!device_ready) {
        map->snapshot.status = TumoSpectrumBandMapStatusNoRadio;
        return false;
    }
    map->device_begun = true;
    if(requested == TumoSpectrumBandMapRadioDual && map->snapshot.external_available && map->device == subghz_devices_get_by_name(TUMOSPECTRUM_BAND_MAP_INTERNAL_NAME)) {
        if(subghz_devices_begin(external)) {
            map->second_device = external;
            map->second_begun = true;
            map->snapshot.radio = TumoSpectrumBandMapRadioDual;
            subghz_devices_reset(external);
            subghz_devices_load_preset(external, FuriHalSubGhzPresetOok650Async, NULL);
        } else {
            subghz_devices_end(external);
            subghz_radio_broker_external_power_off(map->broker, &map->lease);
            map->snapshot.external_available = false;
        }
    }
    subghz_radio_broker_set_selected_device(
        map->broker,
        &map->lease,
        map->snapshot.radio == TumoSpectrumBandMapRadioDual ? SubGhzRadioBrokerDeviceDual :
        map->snapshot.radio == TumoSpectrumBandMapRadioExternal ?
            SubGhzRadioBrokerDeviceExternalCC1101 :
            SubGhzRadioBrokerDeviceInternal);
    subghz_devices_reset(map->device);
    subghz_devices_load_preset(map->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_radio_broker_set_state(map->broker, &map->lease, SubGhzRadioBrokerStateInitialized);
    map->snapshot.status = TumoSpectrumBandMapStatusScanning;
    return true;
}

TumoSpectrumBandMap* tumospectrum_band_map_alloc(void) {
    TumoSpectrumBandMap* map = calloc(1, sizeof(*map));
    if(!map) return NULL;
    map->snapshot.band_index = 2U;
    map->snapshot.status = TumoSpectrumBandMapStatusIdle;
    tumospectrum_band_map_reset_window(map);
    return map;
}

void tumospectrum_band_map_free(TumoSpectrumBandMap* map) {
    if(!map) return;
    tumospectrum_band_map_stop(map);
    free(map);
}

size_t tumospectrum_band_map_band_count(void) {
    return COUNT_OF(tumospectrum_band_map_bands);
}

const TumoSpectrumBandMapBand* tumospectrum_band_map_band(size_t index) {
    return index < COUNT_OF(tumospectrum_band_map_bands) ? &tumospectrum_band_map_bands[index] :
                                                           NULL;
}

bool tumospectrum_band_map_start(TumoSpectrumBandMap* map, bool prefer_external) {
    if(!map) return false;
    tumospectrum_band_map_stop(map);
    map->snapshot.hold = false;
    tumospectrum_band_map_reset_samples(map);
    if(!tumospectrum_band_map_begin_device(map, prefer_external ? TumoSpectrumBandMapRadioExternal : TumoSpectrumBandMapRadioInternal)) {
        TumoSpectrumBandMapStatus status = map->snapshot.status;
        tumospectrum_band_map_stop(map);
        map->snapshot.status = status;
        return false;
    }
    return true;
}

void tumospectrum_band_map_stop(TumoSpectrumBandMap* map) {
    if(!map) return;
    if(map->broker) {
        subghz_radio_broker_set_state(map->broker, &map->lease, SubGhzRadioBrokerStateCleaningUp);
    }
    if(map->device_begun && map->device) {
        subghz_devices_idle(map->device);
        subghz_devices_sleep(map->device);
        subghz_devices_end(map->device);
        map->device_begun = false;
    }
    if(map->second_begun && map->second_device) {
        subghz_devices_idle(map->second_device);
        subghz_devices_sleep(map->second_device);
        subghz_devices_end(map->second_device);
    }
    map->second_begun = false;
    map->second_device = NULL;
    if(map->devices_initialized) {
        subghz_devices_deinit();
        map->devices_initialized = false;
    }
    if(map->broker) {
        subghz_radio_broker_release(map->broker, &map->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        map->broker = NULL;
    }
    map->device = NULL;
    map->snapshot.status = TumoSpectrumBandMapStatusIdle;
}

void tumospectrum_band_map_tick(TumoSpectrumBandMap* map) {
    if(!map || !map->device_begun || map->snapshot.hold ||
       map->snapshot.status != TumoSpectrumBandMapStatusScanning) {
        return;
    }

    if(map->snapshot.radio != TumoSpectrumBandMapRadioInternal) {
        const SubGhzDevice* external = map->second_device ? map->second_device : map->device;
        if(!subghz_devices_is_connect(external)) {
            // Stop both under the existing lease before attempting internal-only
            // restart. Never leave a detached external driver/power context live.
            tumospectrum_band_map_stop(map);
            if(!tumospectrum_band_map_begin_device(map, TumoSpectrumBandMapRadioInternal)) {
                tumospectrum_band_map_stop(map);
                map->snapshot.status = TumoSpectrumBandMapStatusError;
                return;
            }
            tumospectrum_band_map_reset_samples(map);
        }
    }
    const uint8_t radios = map->second_begun ? 2U : 1U;
    const uint8_t limit = TUMOSPECTRUM_BAND_MAP_BINS / radios;
    for(uint8_t sample = 0U; sample < TUMOSPECTRUM_BAND_MAP_BINS_PER_TICK; sample++) {
        const uint8_t bin = map->scan_bin;
        const SubGhzDevice* devices[2] = {map->device, map->second_device};
        bool ready = true;
        for(uint8_t r = 0; r < radios; r++) {
            const uint32_t frequency = tumospectrum_band_map_frequency_for_bin(&map->snapshot, bin + r * limit);
            subghz_devices_idle(devices[r]);
            if(!subghz_devices_is_frequency_valid(devices[r], frequency) ||
               !subghz_devices_set_frequency(devices[r], frequency)) {
                ready = false;
                break;
            }
            subghz_devices_set_rx(devices[r]);
        }
        if(!ready) {
            tumospectrum_band_map_stop(map);
            map->snapshot.status = TumoSpectrumBandMapStatusError;
            return;
        }
        furi_delay_ms(TUMOSPECTRUM_BAND_MAP_SAMPLE_DELAY_MS);
        for(uint8_t r = 0; r < radios; r++)
            map->snapshot.bins[bin + r * limit] = tumospectrum_band_map_clamp_rssi(subghz_devices_get_rssi(devices[r]));

        map->scan_bin++;
        if(map->scan_bin >= limit) {
            map->scan_bin = 0U;
            tumospectrum_band_map_finish_sweep(map);
        }
    }
    subghz_radio_broker_set_state(map->broker, &map->lease, SubGhzRadioBrokerStateRx);
}

bool tumospectrum_band_map_is_running(const TumoSpectrumBandMap* map) {
    return map && map->device_begun &&
           (map->snapshot.status == TumoSpectrumBandMapStatusScanning ||
            map->snapshot.status == TumoSpectrumBandMapStatusHold);
}

bool tumospectrum_band_map_set_band(TumoSpectrumBandMap* map, size_t index) {
    if(!map || index >= COUNT_OF(tumospectrum_band_map_bands)) return false;
    map->snapshot.band_index = index;
    tumospectrum_band_map_reset_window(map);
    return true;
}

bool tumospectrum_band_map_cycle_radio(TumoSpectrumBandMap* map) {
    if(!map) return false;
    const TumoSpectrumBandMapRadio request = (map->snapshot.radio + 1) % 3;
    tumospectrum_band_map_stop(map);
    map->snapshot.hold = false;
    const bool started = tumospectrum_band_map_begin_device(map, request);
    if(!started) {
        tumospectrum_band_map_stop(map);
        return false;
    }
    tumospectrum_band_map_reset_samples(map);
    return true;
}

void tumospectrum_band_map_move_cursor(TumoSpectrumBandMap* map, int8_t delta) {
    if(!map || delta == 0) return;
    int16_t next = (int16_t)map->snapshot.selected_bin + delta;
    if(next < 0) next = 0;
    if(next >= (int16_t)TUMOSPECTRUM_BAND_MAP_BINS) {
        next = TUMOSPECTRUM_BAND_MAP_BINS - 1U;
    }
    map->snapshot.selected_bin = (uint8_t)next;
    map->snapshot.selected_frequency_hz =
        tumospectrum_band_map_frequency_for_bin(&map->snapshot, map->snapshot.selected_bin);
    map->snapshot.selected_rssi_dbm = map->snapshot.bins[map->snapshot.selected_bin];
}

void tumospectrum_band_map_cycle_zoom(TumoSpectrumBandMap* map) {
    if(!map) return;
    const TumoSpectrumBandMapBand* band = &tumospectrum_band_map_bands[map->snapshot.band_index];
    const uint32_t selected = map->snapshot.selected_frequency_hz;
    map->snapshot.zoom =
        map->snapshot.zoom >= TUMOSPECTRUM_BAND_MAP_ZOOM_MAX ? 1U : map->snapshot.zoom * 2U;
    if(map->snapshot.zoom == 1U) {
        map->snapshot.window_start_hz = band->start_hz;
        map->snapshot.window_end_hz = band->end_hz;
    } else {
        const uint32_t full_span = band->end_hz - band->start_hz;
        const uint32_t span = full_span / map->snapshot.zoom;
        uint32_t start = selected > span / 2U ? selected - span / 2U : band->start_hz;
        if(start < band->start_hz) start = band->start_hz;
        if(start + span > band->end_hz) start = band->end_hz - span;
        map->snapshot.window_start_hz = start;
        map->snapshot.window_end_hz = start + span;
    }
    map->snapshot.selected_bin = tumospectrum_band_map_bin_for_frequency(&map->snapshot, selected);
    map->snapshot.selected_frequency_hz =
        tumospectrum_band_map_frequency_for_bin(&map->snapshot, map->snapshot.selected_bin);
    tumospectrum_band_map_reset_samples(map);
}

void tumospectrum_band_map_toggle_hold(TumoSpectrumBandMap* map) {
    if(!map || !map->device_begun) return;
    map->snapshot.hold = !map->snapshot.hold;
    map->snapshot.status = map->snapshot.hold ? TumoSpectrumBandMapStatusHold :
                                                TumoSpectrumBandMapStatusScanning;
    if(map->snapshot.hold) subghz_devices_idle(map->device);
    if(map->snapshot.hold && map->second_begun) subghz_devices_idle(map->second_device);
}

void tumospectrum_band_map_snap_to_peak(TumoSpectrumBandMap* map) {
    if(!map || map->snapshot.peak_frequency_hz == 0U) return;
    map->snapshot.selected_bin =
        tumospectrum_band_map_bin_for_frequency(&map->snapshot, map->snapshot.peak_frequency_hz);
    map->snapshot.selected_frequency_hz =
        tumospectrum_band_map_frequency_for_bin(&map->snapshot, map->snapshot.selected_bin);
    map->snapshot.selected_rssi_dbm = map->snapshot.bins[map->snapshot.selected_bin];
}

void tumospectrum_band_map_clear_peaks(TumoSpectrumBandMap* map) {
    if(!map) return;
    map->snapshot.peak_count = 0;
    memset(map->snapshot.observed_peaks, 0, sizeof(map->snapshot.observed_peaks));
}
bool tumospectrum_band_map_select_peak(TumoSpectrumBandMap* map, unsigned index) {
    if(!map || index >= map->snapshot.peak_count) return false;
    uint32_t frequency = map->snapshot.observed_peaks[index].frequency_hz;
    for(unsigned i = 0; i < COUNT_OF(tumospectrum_band_map_bands); i++) {
        const TumoSpectrumBandMapBand* band = &tumospectrum_band_map_bands[i];
        if(frequency >= band->start_hz && frequency <= band->end_hz) {
            tumospectrum_band_map_set_band(map, i);
            map->snapshot.selected_bin = tumospectrum_band_map_bin_for_frequency(&map->snapshot, frequency);
            map->snapshot.selected_frequency_hz = tumospectrum_band_map_frequency_for_bin(&map->snapshot, map->snapshot.selected_bin);
            return true;
        }
    }
    return false;
}

void tumospectrum_band_map_get_snapshot(
    const TumoSpectrumBandMap* map,
    TumoSpectrumBandMapSnapshot* snapshot) {
    if(!snapshot) return;
    if(!map) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->status = TumoSpectrumBandMapStatusError;
        return;
    }
    *snapshot = map->snapshot;
}
