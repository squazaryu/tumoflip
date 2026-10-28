#include "sensor_runtime.h"
#include "../../../applications_user/weather_editor/protocols/protocol_items.h"
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <toolbox/level_duration.h>
#include <furi_hal_rtc.h>
#include <math.h>
#include <string.h>

static bool sensor_read_u32(FlipperFormat* format, const char* key, uint32_t* value) {
    return flipper_format_rewind(format) && flipper_format_read_uint32(format, key, value, 1);
}

static void sensor_decoded(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder, void* context) {
    UNUSED(receiver);
    SensorRuntime* runtime = context;
    SubGhzRadioPreset preset = {.name = runtime->preset_name, .frequency = runtime->frequency};
    if(subghz_protocol_decoder_base_serialize(decoder, runtime->format, &preset) != SubGhzProtocolStatusOk) return;
    uint32_t id, channel, humidity, battery;
    float temperature;
    if(!sensor_read_u32(runtime->format, "Id", &id) ||
       !sensor_read_u32(runtime->format, "Ch", &channel) || channel > 255U) return;
    SensorPoint point;
    memset(&point, 0, sizeof(point));
    point.timestamp = furi_hal_rtc_get_timestamp();
    if(sensor_read_u32(runtime->format, "Hum", &humidity) && humidity <= 100U) {
        point.fields |= SensorHumidity;
        point.humidity = humidity;
    }
    if(sensor_read_u32(runtime->format, "Batt", &battery) && battery <= 1U) {
        point.fields |= SensorBattery;
        point.battery_low = battery;
    }
    if(flipper_format_rewind(runtime->format) &&
       flipper_format_read_float(runtime->format, "Temp", &temperature, 1) &&
       isfinite(temperature) && temperature >= -80.0f && temperature <= 100.0f) {
        point.fields |= SensorTemperature;
        point.temperature = (int16_t)lroundf(temperature * 10.0f);
    }
    sensor_cards_add(runtime->cards, decoder->protocol->name, id, channel, runtime->frequency, &point);
}

static void sensor_rx(bool level, uint32_t duration, void* context) {
    SensorRuntime* runtime = context;
    const LevelDuration sample = level_duration_make(level, duration);
    if(furi_stream_buffer_send(runtime->stream, &sample, sizeof(sample), 0) != sizeof(sample))
        runtime->overrun = true;
}

void sensor_runtime_stop(SensorRuntime* runtime) {
    if(!runtime) return;
    if(runtime->listening) {
        subghz_devices_stop_async_rx(runtime->device);
        runtime->listening = false;
    }
    if(runtime->device) {
        subghz_devices_sleep(runtime->device);
        subghz_devices_end(runtime->device);
        runtime->device = NULL;
    }
    if(runtime->devices) {
        subghz_devices_deinit();
        runtime->devices = false;
    }
    if(runtime->broker) {
        subghz_radio_broker_release(runtime->broker, &runtime->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        runtime->broker = NULL;
    }
    if(runtime->receiver) subghz_receiver_free(runtime->receiver);
    if(runtime->environment) subghz_environment_free(runtime->environment);
    if(runtime->format) flipper_format_free(runtime->format);
    if(runtime->preset_name) furi_string_free(runtime->preset_name);
    if(runtime->stream) furi_stream_buffer_free(runtime->stream);
    runtime->receiver = NULL;
    runtime->environment = NULL;
    runtime->format = NULL;
    runtime->preset_name = NULL;
    runtime->stream = NULL;
    runtime->overrun = false;
}

bool sensor_runtime_start(SensorRuntime* runtime, SensorCards* cards, uint32_t frequency) {
    if(!runtime || !sensor_cards_valid(cards)) return false;
    sensor_runtime_stop(runtime);
    // All 25 decoders are external. Refuse a low-memory launch before their allocations.
    if(memmgr_get_free_heap() < 24000U ||
       (frequency != 315000000U && frequency != 433920000U && frequency != 868350000U)) return false;
    runtime->cards = cards;
    runtime->frequency = frequency;
    runtime->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!subghz_radio_broker_acquire(runtime->broker, "sensor_watch", 500U, &runtime->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        runtime->broker = NULL;
        return false;
    }
    runtime->stream = furi_stream_buffer_alloc(4096U, sizeof(LevelDuration));
    runtime->format = flipper_format_string_alloc();
    runtime->preset_name = furi_string_alloc_set("AM650");
    runtime->environment = subghz_environment_alloc();
    if(!runtime->stream || !runtime->format || !runtime->environment) {
        sensor_runtime_stop(runtime);
        return false;
    }
    subghz_environment_set_protocol_registry(runtime->environment, &weather_station_protocol_registry);
    runtime->receiver = subghz_receiver_alloc_init(runtime->environment);
    if(!runtime->receiver) {
        sensor_runtime_stop(runtime);
        return false;
    }
    subghz_receiver_set_filter(runtime->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_modulation_filter(runtime->receiver, SubGhzProtocolFlag_AM);
    subghz_receiver_set_rx_callback(runtime->receiver, sensor_decoded, runtime);
    subghz_receiver_reset(runtime->receiver);
    subghz_devices_init();
    runtime->devices = true;
    runtime->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!runtime->device || !subghz_devices_is_frequency_valid(runtime->device, frequency)) {
        sensor_runtime_stop(runtime);
        return false;
    }
    subghz_devices_reset(runtime->device);
    subghz_devices_load_preset(runtime->device, FuriHalSubGhzPresetOok650Async, NULL);
    if(!subghz_devices_set_frequency(runtime->device, frequency)) {
        sensor_runtime_stop(runtime);
        return false;
    }
    subghz_devices_flush_rx(runtime->device);
    subghz_devices_set_rx(runtime->device);
    subghz_radio_broker_set_selected_device(runtime->broker, &runtime->lease, SubGhzRadioBrokerDeviceInternal);
    subghz_radio_broker_set_state(runtime->broker, &runtime->lease, SubGhzRadioBrokerStateAsyncRx);
    subghz_devices_start_async_rx(runtime->device, sensor_rx, runtime);
    runtime->listening = true;
    return true;
}

bool sensor_runtime_tick(SensorRuntime* runtime) {
    if(!runtime || !runtime->listening) return false;
    if(runtime->overrun) {
        FURI_CRITICAL_ENTER();
        runtime->overrun = false;
        FURI_CRITICAL_EXIT();
        furi_stream_buffer_reset(runtime->stream);
        subghz_receiver_reset(runtime->receiver);
        if(runtime->lost != UINT32_MAX) runtime->lost++;
    }
    LevelDuration sample;
    bool received = false;
    for(unsigned i = 0; i < 1024U; i++) {
        if(furi_stream_buffer_receive(runtime->stream, &sample, sizeof(sample), 0) != sizeof(sample)) break;
        const uint32_t duration = level_duration_get_duration(sample);
        if(duration < 40U || duration > 1000000U) subghz_receiver_reset(runtime->receiver);
        else subghz_receiver_decode(runtime->receiver, level_duration_get_level(sample), duration);
        received = true;
    }
    return received;
}
