#include "subghz_frequency_analyzer_worker.h"
#include <lib/drivers/cc1101.h>

#include <furi.h>
#include <float_tools.h>

#define TAG "SubghzFrequencyAnalyzerWorker"

#define SUBGHZ_FREQUENCY_ANALYZER_THRESHOLD -97.0f
#define PRESET_SCAN_SAMPLE_COUNT            10U
#define PRESET_SCAN_SAMPLE_DELAY_MS         12U
#define PRESET_SCAN_MAX_RESULTS             32U

static const uint8_t subghz_preset_ook_58khz[][2] = {
    {CC1101_MDMCFG4, 0b11110111}, // Rx BW filter is 58.035714kHz
    /* End  */
    {0, 0},
};

static const uint8_t subghz_preset_ook_650khz[][2] = {
    {CC1101_MDMCFG4, 0b00010111}, // Rx BW filter is 650.000kHz
    /* End  */
    {0, 0},
};

struct SubGhzFrequencyAnalyzerWorker {
    FuriThread* thread;

    volatile bool worker_running;
    uint8_t sample_hold_counter;
    FrequencyRSSI frequency_rssi_buf;
    SubGhzSetting* setting;
    SubGhzTxRx* txrx;

    SubGhzFrequencyAnalyzerWorkerMode mode;
    uint32_t preset_frequency;
    SubGhzFrequencyAnalyzerPresetResult* preset_results;
    size_t preset_result_capacity;
    size_t preset_result_count;

    float filVal;
    float trigger_level;

    SubGhzFrequencyAnalyzerWorkerPairCallback pair_callback;
    SubGhzFrequencyAnalyzerWorkerPresetCallback preset_callback;
    void* context;
};

static void subghz_frequency_analyzer_worker_load_registers(const uint8_t data[][2]) {
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    size_t i = 0;
    while(data[i][0]) {
        cc1101_write_reg(&furi_hal_spi_bus_handle_subghz, data[i][0], data[i][1]);
        i++;
    }
    furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

// running average with adaptive coefficient
static uint32_t subghz_frequency_analyzer_worker_expRunningAverageAdaptive(
    SubGhzFrequencyAnalyzerWorker* instance,
    uint32_t newVal) {
    float k;
    float newValFloat = newVal;
    // the sharpness of the filter depends on the absolute value of the difference
    if(fabsf(newValFloat - instance->filVal) > 500000.f)
        k = 0.9;
    else
        k = 0.03;

    instance->filVal += (newValFloat - instance->filVal) * k;
    return (uint32_t)instance->filVal;
}

/** Worker thread
 *
 * @param context
 * @return exit code
 */
static int32_t subghz_frequency_analyzer_worker_frequency_thread(void* context) {
    SubGhzFrequencyAnalyzerWorker* instance = context;

    FrequencyRSSI frequency_rssi = {
        .frequency_coarse = 0, .rssi_coarse = 0, .frequency_fine = 0, .rssi_fine = 0};
    float rssi = 0;
    uint32_t frequency = 0;
    float rssi_temp = 0;
    uint32_t frequency_temp = 0;

    //Start CC1101
    furi_hal_subghz_reset();

    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    cc1101_flush_rx(&furi_hal_spi_bus_handle_subghz);
    cc1101_flush_tx(&furi_hal_spi_bus_handle_subghz);
    cc1101_write_reg(&furi_hal_spi_bus_handle_subghz, CC1101_IOCFG0, CC1101IocfgHW);
    cc1101_write_reg(&furi_hal_spi_bus_handle_subghz, CC1101_MDMCFG3,
                     0b01111111); // symbol rate
    cc1101_write_reg(
        &furi_hal_spi_bus_handle_subghz,
        CC1101_AGCCTRL2,
        0b00000111); // 00 - DVGA all; 000 - MAX LNA+LNA2; 111 - MAGN_TARGET 42 dB
    cc1101_write_reg(
        &furi_hal_spi_bus_handle_subghz,
        CC1101_AGCCTRL1,
        0b00001000); // 0; 0 - LNA 2 gain is decreased to minimum before decreasing LNA gain; 00 - Relative carrier sense threshold disabled; 1000 - Absolute carrier sense threshold disabled
    cc1101_write_reg(
        &furi_hal_spi_bus_handle_subghz,
        CC1101_AGCCTRL0,
        0b00110000); // 00 - No hysteresis, medium asymmetric dead zone, medium gain ; 11 - 64 samples agc; 00 - Normal AGC, 00 - 4dB boundary

    furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);

    furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);

    while(instance->worker_running) {
        furi_delay_ms(10);

        float rssi_min = 26.0f;
        float rssi_avg = 0;
        size_t rssi_avg_samples = 0;

        frequency_rssi.rssi_coarse = -127.0f;
        frequency_rssi.rssi_fine = -127.0f;
        furi_hal_subghz_idle();
        subghz_frequency_analyzer_worker_load_registers(subghz_preset_ook_650khz);

        // First stage: coarse scan
        for(size_t i = 0; i < subghz_setting_get_frequency_count(instance->setting); i++) {
            uint32_t current_frequency = subghz_setting_get_frequency(instance->setting, i);
            if(furi_hal_subghz_is_frequency_valid(current_frequency) &&
               (((current_frequency != 462750000) && (current_frequency != 467750000) &&
                 (current_frequency != 464000000)) &&
                (current_frequency <= 920000000))) {
                furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
                cc1101_switch_to_idle(&furi_hal_spi_bus_handle_subghz);
                frequency = cc1101_set_frequency(
                    &furi_hal_spi_bus_handle_subghz,
                    subghz_setting_get_frequency(instance->setting, i));

                cc1101_calibrate(&furi_hal_spi_bus_handle_subghz);

                furi_check(cc1101_wait_status_state(
                    &furi_hal_spi_bus_handle_subghz, CC1101StateIDLE, 10000));

                cc1101_switch_to_rx(&furi_hal_spi_bus_handle_subghz);
                furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);

                furi_delay_ms(2);

                rssi = furi_hal_subghz_get_rssi();

                rssi_avg += rssi;
                rssi_avg_samples++;

                if(rssi < rssi_min) rssi_min = rssi;

                if(frequency_rssi.rssi_coarse < rssi) {
                    frequency_rssi.rssi_coarse = rssi;
                    frequency_rssi.frequency_coarse = frequency;
                }
            }
        }

        FURI_LOG_T(
            TAG,
            "RSSI: avg %f, max %f at %lu, min %f",
            (double)(rssi_avg / rssi_avg_samples),
            (double)frequency_rssi.rssi_coarse,
            frequency_rssi.frequency_coarse,
            (double)rssi_min);

        // Second stage: fine scan
        if(frequency_rssi.rssi_coarse > instance->trigger_level) {
            furi_hal_subghz_idle();
            subghz_frequency_analyzer_worker_load_registers(subghz_preset_ook_58khz);
            //for example -0.3 ... 433.92 ... +0.3 step 20KHz
            for(uint32_t i = frequency_rssi.frequency_coarse - 300000;
                i < frequency_rssi.frequency_coarse + 300000;
                i += 20000) {
                if(furi_hal_subghz_is_frequency_valid(i)) {
                    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
                    cc1101_switch_to_idle(&furi_hal_spi_bus_handle_subghz);
                    frequency = cc1101_set_frequency(&furi_hal_spi_bus_handle_subghz, i);

                    cc1101_calibrate(&furi_hal_spi_bus_handle_subghz);

                    furi_check(cc1101_wait_status_state(
                        &furi_hal_spi_bus_handle_subghz, CC1101StateIDLE, 10000));

                    cc1101_switch_to_rx(&furi_hal_spi_bus_handle_subghz);
                    furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);

                    furi_delay_ms(2);

                    rssi = furi_hal_subghz_get_rssi();

                    FURI_LOG_T(TAG, "#:%lu:%f", frequency, (double)rssi);

                    if(frequency_rssi.rssi_fine < rssi) {
                        frequency_rssi.rssi_fine = rssi;
                        frequency_rssi.frequency_fine = frequency;
                    }
                }
            }
        }

        // Deliver results fine
        if(frequency_rssi.rssi_fine > instance->trigger_level) {
            FURI_LOG_D(
                TAG, "=:%lu:%f", frequency_rssi.frequency_fine, (double)frequency_rssi.rssi_fine);

            instance->sample_hold_counter = 20;
            rssi_temp = frequency_rssi.rssi_fine;
            frequency_temp = frequency_rssi.frequency_fine;

            if(!float_is_equal(instance->filVal, 0.f)) {
                frequency_rssi.frequency_fine =
                    subghz_frequency_analyzer_worker_expRunningAverageAdaptive(
                        instance, frequency_rssi.frequency_fine);
            }
            // Deliver callback
            if(instance->pair_callback) {
                instance->pair_callback(
                    instance->context,
                    frequency_rssi.frequency_fine,
                    frequency_rssi.rssi_fine,
                    true);
            }
        } else if( // Deliver results coarse
            (frequency_rssi.rssi_coarse > instance->trigger_level) &&
            (instance->sample_hold_counter < 10)) {
            FURI_LOG_D(
                TAG,
                "~:%lu:%f",
                frequency_rssi.frequency_coarse,
                (double)frequency_rssi.rssi_coarse);

            instance->sample_hold_counter = 20;
            rssi_temp = frequency_rssi.rssi_coarse;
            frequency_temp = frequency_rssi.frequency_coarse;
            if(!float_is_equal(instance->filVal, 0.f)) {
                frequency_rssi.frequency_coarse =
                    subghz_frequency_analyzer_worker_expRunningAverageAdaptive(
                        instance, frequency_rssi.frequency_coarse);
            }
            // Deliver callback
            if(instance->pair_callback) {
                instance->pair_callback(
                    instance->context,
                    frequency_rssi.frequency_coarse,
                    frequency_rssi.rssi_coarse,
                    true);
            }
        } else {
            if(instance->sample_hold_counter > 0) {
                instance->sample_hold_counter--;
                if(instance->sample_hold_counter == 18) {
                    if(instance->pair_callback) {
                        instance->pair_callback(
                            instance->context, frequency_temp, rssi_temp, false);
                    }
                }
            } else {
                instance->filVal = 0;
                if(instance->pair_callback)
                    instance->pair_callback(instance->context, 0, 0, false);
            }
        }
    }

    //Stop CC1101
    furi_hal_subghz_idle();
    // Clear analyzer-specific AGC/BW settings and restore the boot RF-switch state.
    furi_hal_subghz_reset();
    furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);
    furi_hal_subghz_sleep();

    return 0;
}

static void
    subghz_frequency_analyzer_worker_sort_preset_results(SubGhzFrequencyAnalyzerWorker* instance) {
    for(size_t i = 1; i < instance->preset_result_count; i++) {
        SubGhzFrequencyAnalyzerPresetResult candidate = instance->preset_results[i];
        size_t position = i;
        while(position > 0 && instance->preset_results[position - 1].score < candidate.score) {
            instance->preset_results[position] = instance->preset_results[position - 1];
            position--;
        }
        instance->preset_results[position] = candidate;
    }
}

static void subghz_frequency_analyzer_worker_report_preset(
    SubGhzFrequencyAnalyzerWorker* instance,
    size_t completed,
    size_t total,
    bool complete,
    const SubGhzFrequencyAnalyzerPresetResult* result) {
    if(!instance->preset_callback) return;

    SubGhzFrequencyAnalyzerPresetProgress progress = {
        .frequency = instance->preset_frequency,
        .completed = completed,
        .total = total,
        .complete = complete,
        .valid = result != NULL,
    };
    if(result) progress.result = *result;
    instance->preset_callback(instance->context, &progress);
}

static int32_t subghz_frequency_analyzer_worker_preset_thread(void* context) {
    SubGhzFrequencyAnalyzerWorker* instance = context;
    const size_t preset_count = instance->preset_result_capacity;
    instance->preset_result_count = 0;

    if(instance->preset_frequency == 0 || preset_count == 0) {
        subghz_frequency_analyzer_worker_report_preset(instance, 0, preset_count, true, NULL);
        return 0;
    }

    for(size_t preset_index = 0; preset_index < preset_count && instance->worker_running;
        preset_index++) {
        if(!subghz_txrx_analyzer_begin(instance->txrx, preset_index, instance->preset_frequency)) {
            continue;
        }

        float peak_rssi = -127.0f;
        float noise_floor = 0.0f;
        float rssi_sum = 0.0f;
        size_t sample_count = 0;
        for(size_t sample = 0; sample < PRESET_SCAN_SAMPLE_COUNT && instance->worker_running;
            sample++) {
            furi_delay_ms(PRESET_SCAN_SAMPLE_DELAY_MS);
            const float rssi = subghz_txrx_radio_device_get_rssi(instance->txrx);
            if(sample_count == 0 || rssi < noise_floor) noise_floor = rssi;
            if(rssi > peak_rssi) peak_rssi = rssi;
            rssi_sum += rssi;
            sample_count++;
        }
        subghz_txrx_analyzer_end(instance->txrx);

        if(!instance->worker_running || sample_count == 0) break;

        SubGhzFrequencyAnalyzerPresetResult* result =
            &instance->preset_results[instance->preset_result_count++];
        result->preset_index = preset_index;
        strlcpy(
            result->preset_name,
            subghz_setting_get_preset_name(instance->setting, preset_index),
            sizeof(result->preset_name));
        result->peak_rssi = peak_rssi;
        result->average_rssi = rssi_sum / sample_count;
        result->noise_floor = noise_floor;
        result->score = result->average_rssi + (peak_rssi - result->average_rssi) * 0.35f;

        subghz_frequency_analyzer_worker_report_preset(
            instance, preset_index + 1, preset_count, false, result);
    }

    subghz_txrx_analyzer_end(instance->txrx);
    if(!instance->worker_running) return 0;

    subghz_frequency_analyzer_worker_sort_preset_results(instance);
    const SubGhzFrequencyAnalyzerPresetResult* best =
        instance->preset_result_count > 0 ? &instance->preset_results[0] : NULL;
    subghz_frequency_analyzer_worker_report_preset(
        instance, preset_count, preset_count, true, best);
    return 0;
}

static int32_t subghz_frequency_analyzer_worker_thread(void* context) {
    SubGhzFrequencyAnalyzerWorker* instance = context;
    if(instance->mode == SubGhzFrequencyAnalyzerWorkerModePreset) {
        return subghz_frequency_analyzer_worker_preset_thread(context);
    }
    return subghz_frequency_analyzer_worker_frequency_thread(context);
}

SubGhzFrequencyAnalyzerWorker* subghz_frequency_analyzer_worker_alloc(void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzerWorker* instance = calloc(1, sizeof(SubGhzFrequencyAnalyzerWorker));

    instance->thread = furi_thread_alloc_ex(
        "SubGhzFAWorker", 2048, subghz_frequency_analyzer_worker_thread, instance);
    SubGhz* subghz = context;
    instance->txrx = subghz->txrx;
    instance->setting = subghz_txrx_get_setting(instance->txrx);
    instance->trigger_level = subghz->last_settings->frequency_analyzer_trigger;
    instance->mode = SubGhzFrequencyAnalyzerWorkerModeFrequency;
    instance->preset_frequency = subghz->last_settings->frequency;
    instance->preset_result_capacity = subghz_setting_get_preset_count(instance->setting);
    if(instance->preset_result_capacity > PRESET_SCAN_MAX_RESULTS) {
        instance->preset_result_capacity = PRESET_SCAN_MAX_RESULTS;
    }
    if(instance->preset_result_capacity > 0) {
        instance->preset_results =
            calloc(instance->preset_result_capacity, sizeof(SubGhzFrequencyAnalyzerPresetResult));
    }
    //instance->trigger_level = SUBGHZ_FREQUENCY_ANALYZER_THRESHOLD;
    return instance;
}

void subghz_frequency_analyzer_worker_free(SubGhzFrequencyAnalyzerWorker* instance) {
    furi_assert(instance);

    furi_thread_free(instance->thread);
    free(instance->preset_results);
    free(instance);
}

void subghz_frequency_analyzer_worker_set_pair_callback(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerPairCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(context);
    instance->pair_callback = callback;
    instance->context = context;
}

void subghz_frequency_analyzer_worker_set_preset_callback(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerPresetCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(context);
    instance->preset_callback = callback;
    instance->context = context;
}

void subghz_frequency_analyzer_worker_set_mode(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerMode mode,
    uint32_t frequency) {
    furi_assert(instance);
    furi_assert(!instance->worker_running);
    instance->mode = mode;
    if(frequency > 0) instance->preset_frequency = frequency;
}

size_t subghz_frequency_analyzer_worker_get_preset_result_count(
    SubGhzFrequencyAnalyzerWorker* instance) {
    furi_assert(instance);
    return instance->preset_result_count;
}

bool subghz_frequency_analyzer_worker_get_preset_result(
    SubGhzFrequencyAnalyzerWorker* instance,
    size_t rank,
    SubGhzFrequencyAnalyzerPresetResult* result) {
    furi_assert(instance);
    furi_assert(result);
    if(rank >= instance->preset_result_count) return false;
    *result = instance->preset_results[rank];
    return true;
}

void subghz_frequency_analyzer_worker_start(SubGhzFrequencyAnalyzerWorker* instance) {
    furi_assert(instance);
    furi_assert(!instance->worker_running);

    instance->worker_running = true;
    if(instance->mode == SubGhzFrequencyAnalyzerWorkerModePreset) {
        instance->preset_result_count = 0;
    }

    furi_thread_start(instance->thread);
}

void subghz_frequency_analyzer_worker_stop(SubGhzFrequencyAnalyzerWorker* instance) {
    furi_assert(instance);
    furi_assert(instance->worker_running);

    instance->worker_running = false;

    furi_thread_join(instance->thread);
}

bool subghz_frequency_analyzer_worker_is_running(SubGhzFrequencyAnalyzerWorker* instance) {
    furi_assert(instance);
    return instance->worker_running;
}

void subghz_frequency_analyzer_worker_set_trigger_level(
    SubGhzFrequencyAnalyzerWorker* instance,
    float value) {
    instance->trigger_level = value;
}

float subghz_frequency_analyzer_worker_get_trigger_level(SubGhzFrequencyAnalyzerWorker* instance) {
    return instance->trigger_level;
}

uint32_t subghz_frequency_analyzer_get_nearest_frequency(
    SubGhzFrequencyAnalyzerWorker* instance,
    uint32_t input) {
    uint32_t prev_freq = 0;
    uint32_t result = 0;
    uint32_t current;

    for(size_t i = 0; i < subghz_setting_get_frequency_count(instance->setting); i++) {
        current = subghz_setting_get_frequency(instance->setting, i);
        if(current == 0) {
            continue;
        }
        if(current == input) {
            result = current;
            break;
        }
        if(current > input && prev_freq < input) {
            if(current - input < input - prev_freq) {
                result = current;
            } else {
                result = prev_freq;
            }
            break;
        }
        prev_freq = current;
    }

    return result;
}
