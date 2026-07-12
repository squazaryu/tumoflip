#include "tumomodule_drivers.h"

#include <furi.h>
#include <furi_hal_i2c.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#include "../tumovgm_bridge/protocol/protocol.h"

enum {
    TumoModuleI2cTimeoutMs = 5,
    TumoModuleBmeMeasureTimeoutMs = 30,
    TumoModuleVgmTimeoutMs = 600,
    TumoModuleVgmRxSize = TumovgmFrameMaxSize,
    TumoModuleVgmHelloPayloadSize = 12,
};

typedef struct {
    FuriStreamBuffer* stream;
} TumoModuleVgmReceiver;

static void tumomodule_result(
    TumoModuleProbeResult* result,
    TumoModuleProbeStatus status,
    const char* detail) {
    result->status = status;
    strlcpy(result->detail, detail ? detail : "", sizeof(result->detail));
}

const char* tumomodule_probe_status_name(TumoModuleProbeStatus status) {
    switch(status) {
    case TumoModuleProbeRunning:
        return "Probing";
    case TumoModuleProbeDetected:
        return "Detected";
    case TumoModuleProbeMissing:
        return "Not detected";
    case TumoModuleProbeBusy:
        return "Resource busy";
    case TumoModuleProbeIncompatible:
        return "Incompatible";
    case TumoModuleProbeError:
        return "Probe error";
    case TumoModuleProbeIdle:
    default:
        return "Ready to probe";
    }
}

static void tumomodule_vgm_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    TumoModuleVgmReceiver* receiver = context;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            const uint8_t value = furi_hal_serial_async_rx(handle);
            furi_stream_buffer_send(receiver->stream, &value, 1, 0);
        }
    }
}

static uint64_t tumomodule_read_u64(const uint8_t* data) {
    uint64_t value = 0;
    for(uint8_t index = 0; index < 8; index++) {
        value |= (uint64_t)data[index] << (index * 8);
    }
    return value;
}

static bool tumomodule_vgm_exchange(
    FuriHalSerialHandle* serial,
    FuriStreamBuffer* stream,
    uint16_t sequence,
    uint16_t message,
    const uint8_t* payload,
    uint16_t payload_length,
    TumovgmFrame* response,
    uint8_t* rx,
    size_t rx_capacity) {
    furi_stream_buffer_reset(stream);
    const TumovgmFrame request = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = TUMOVGM_PROTOCOL_MINOR,
        .kind = TumovgmFrameKindRequest,
        .sequence = sequence,
        .message = message,
        .payload_length = payload_length,
        .payload = payload,
    };
    uint8_t tx[TumovgmFrameMaxSize];
    size_t tx_size = 0;
    if(tumovgm_frame_encode(&request, tx, sizeof(tx), &tx_size) != TumovgmCodecStatusOk) {
        return false;
    }
    furi_hal_serial_tx(serial, tx, tx_size);
    furi_hal_serial_tx_wait_complete(serial);

    size_t rx_size = 0;
    const uint32_t started = furi_get_tick();
    const uint32_t timeout = furi_ms_to_ticks(TumoModuleVgmTimeoutMs);
    while((uint32_t)(furi_get_tick() - started) < timeout && rx_size < rx_capacity) {
        rx_size += furi_stream_buffer_receive(
            stream, rx + rx_size, rx_capacity - rx_size, furi_ms_to_ticks(20));
        size_t consumed = 0;
        const TumovgmCodecStatus codec = tumovgm_frame_decode(rx, rx_size, response, &consumed);
        if(codec == TumovgmCodecStatusNeedMore) continue;
        if(codec == TumovgmCodecStatusOk && response->sequence == sequence &&
           response->message == message) {
            return true;
        }
        if(consumed == 0) return false;
        if(consumed >= rx_size) {
            rx_size = 0;
        } else {
            memmove(rx, rx + consumed, rx_size - consumed);
            rx_size -= consumed;
        }
    }
    return false;
}

static void tumomodule_probe_vgm(
    const TumoModuleManifest* manifest,
    TumoModuleDriverContext* context,
    TumoModuleProbeResult* result) {
    FuriHalSerialHandle* serial = NULL;
    FuriStreamBuffer* stream = furi_stream_buffer_alloc(TumoModuleVgmRxSize, 1);
    TumoModuleVgmReceiver receiver = {.stream = stream};
    bool serial_started = false;
    expansion_disable(context->expansion);
    serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(serial == NULL) {
        tumomodule_result(result, TumoModuleProbeBusy, "UART owned by another app");
        expansion_enable(context->expansion);
        furi_stream_buffer_free(stream);
        return;
    }

    furi_hal_serial_init(serial, manifest->baud);
    furi_hal_serial_configure_framing(
        serial, FuriHalSerialDataBits8, FuriHalSerialParityNone, FuriHalSerialStopBits1);
    furi_hal_serial_async_rx_start(serial, tumomodule_vgm_rx_callback, &receiver, false);
    serial_started = true;
    furi_delay_ms(25);

    uint8_t hello_payload[TumoModuleVgmHelloPayloadSize] = {0};
    hello_payload[0] = TumovgmRoleFlipper;
    hello_payload[2] = (uint8_t)TUMOVGM_PROTOCOL_MAX_PAYLOAD;
    hello_payload[3] = (uint8_t)(TUMOVGM_PROTOCOL_MAX_PAYLOAD >> 8);
    uint8_t rx[TumoModuleVgmRxSize];
    TumovgmFrame response;
    if(!tumomodule_vgm_exchange(
           serial,
           stream,
           1,
           TumovgmMessageHello,
           hello_payload,
           sizeof(hello_payload),
           &response,
           rx,
           sizeof(rx))) {
        tumomodule_result(result, TumoModuleProbeMissing, "No bounded HELLO response");
        goto cleanup;
    }
    if(response.major != TUMOVGM_PROTOCOL_MAJOR) {
        tumomodule_result(result, TumoModuleProbeIncompatible, "Protocol major mismatch");
        goto cleanup;
    }
    if(response.kind != TumovgmFrameKindResponse ||
       response.payload_length != TumoModuleVgmHelloPayloadSize ||
       response.payload[0] != TumovgmRoleVgm) {
        tumomodule_result(result, TumoModuleProbeError, "Invalid HELLO response");
        goto cleanup;
    }

    const uint8_t negotiated_minor = response.payload[1];
    const uint64_t capabilities = tumomodule_read_u64(response.payload + 4);
    if(negotiated_minor < 2 || (capabilities & (UINT64_C(1) << TumovgmCapabilityBitImu)) == 0) {
        tumomodule_result(result, TumoModuleProbeIncompatible, "IMU service unavailable");
        goto cleanup;
    }
    if(!tumomodule_vgm_exchange(
           serial, stream, 2, TumovgmMessageImuInfo, NULL, 0, &response, rx, sizeof(rx)) ||
       response.kind != TumovgmFrameKindResponse || response.payload_length != 12) {
        tumomodule_result(result, TumoModuleProbeError, "IMU_INFO failed");
        goto cleanup;
    }
    snprintf(
        result->detail,
        sizeof(result->detail),
        "VGM IMU ID %02X health %u",
        response.payload[1],
        response.payload[0]);
    result->status = TumoModuleProbeDetected;

cleanup:
    if(serial_started) furi_hal_serial_async_rx_stop(serial);
    furi_hal_serial_deinit(serial);
    furi_hal_serial_control_release(serial);
    furi_stream_buffer_free(stream);
    expansion_enable(context->expansion);
}

static bool tumomodule_bme280_read_id(uint8_t address, uint8_t* chip_id) {
    uint8_t reg = 0xD0;
    return furi_hal_i2c_trx(
        &furi_hal_i2c_handle_external,
        (uint8_t)(address << 1),
        &reg,
        1,
        chip_id,
        1,
        TumoModuleI2cTimeoutMs);
}

static bool tumomodule_bme280_write(uint8_t address, uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return furi_hal_i2c_tx(
        &furi_hal_i2c_handle_external,
        (uint8_t)(address << 1),
        data,
        sizeof(data),
        TumoModuleI2cTimeoutMs);
}

static uint16_t tumomodule_read_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool tumomodule_bme280_sample_temperature(uint8_t address, int32_t* temperature_c_x100) {
    uint8_t calibration[6];
    uint8_t reg = 0x88;
    if(!furi_hal_i2c_trx(
           &furi_hal_i2c_handle_external,
           (uint8_t)(address << 1),
           &reg,
           1,
           calibration,
           sizeof(calibration),
           TumoModuleI2cTimeoutMs)) {
        return false;
    }
    const uint16_t dig_t1 = tumomodule_read_u16_le(calibration);
    const int16_t dig_t2 = (int16_t)tumomodule_read_u16_le(calibration + 2);
    const int16_t dig_t3 = (int16_t)tumomodule_read_u16_le(calibration + 4);
    if(dig_t1 == 0) return false;

    if(!tumomodule_bme280_write(address, 0xF4, 0x25)) return false;
    const uint32_t started = furi_get_tick();
    const uint32_t timeout = furi_ms_to_ticks(TumoModuleBmeMeasureTimeoutMs);
    uint8_t status = 0x08;
    while((status & 0x08) != 0 && (uint32_t)(furi_get_tick() - started) < timeout) {
        reg = 0xF3;
        if(!furi_hal_i2c_trx(
               &furi_hal_i2c_handle_external,
               (uint8_t)(address << 1),
               &reg,
               1,
               &status,
               1,
               TumoModuleI2cTimeoutMs)) {
            return false;
        }
        if((status & 0x08) != 0) furi_delay_ms(2);
    }
    if((status & 0x08) != 0) return false;

    uint8_t raw[3];
    reg = 0xFA;
    if(!furi_hal_i2c_trx(
           &furi_hal_i2c_handle_external,
           (uint8_t)(address << 1),
           &reg,
           1,
           raw,
           sizeof(raw),
           TumoModuleI2cTimeoutMs)) {
        return false;
    }
    const int32_t adc_t = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) |
                          ((int32_t)raw[2] >> 4);
    const int32_t var1 = ((((adc_t >> 3) - ((int32_t)dig_t1 << 1))) * ((int32_t)dig_t2)) >> 11;
    const int32_t var2 =
        (((((adc_t >> 4) - (int32_t)dig_t1) * ((adc_t >> 4) - (int32_t)dig_t1)) >> 12) *
         (int32_t)dig_t3) >>
        14;
    const int32_t t_fine = var1 + var2;
    *temperature_c_x100 = ((t_fine * 5) + 128) >> 8;
    return true;
}

static void
    tumomodule_probe_bme280(const TumoModuleManifest* manifest, TumoModuleProbeResult* result) {
    const uint8_t addresses[] = {0x76, 0x77};
    bool compatible_device_seen = false;
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    for(size_t index = 0; index < COUNT_OF(addresses); index++) {
        const uint8_t address = addresses[index];
        if(manifest->address != 0 && manifest->address != address) continue;
        if(!furi_hal_i2c_is_device_ready(
               &furi_hal_i2c_handle_external, (uint8_t)(address << 1), TumoModuleI2cTimeoutMs)) {
            continue;
        }
        uint8_t chip_id = 0;
        if(tumomodule_bme280_read_id(address, &chip_id) && chip_id == 0x60) {
            compatible_device_seen = true;
            int32_t temperature_c_x100 = 0;
            if(!tumomodule_bme280_sample_temperature(address, &temperature_c_x100)) {
                break;
            }
            const bool negative = temperature_c_x100 < 0;
            const uint32_t absolute =
                (uint32_t)(negative ? -temperature_c_x100 : temperature_c_x100);
            snprintf(
                result->detail,
                sizeof(result->detail),
                "BME280 %s%lu.%02lu C",
                negative ? "-" : "",
                (unsigned long)(absolute / 100U),
                (unsigned long)(absolute % 100U));
            result->status = TumoModuleProbeDetected;
            furi_hal_i2c_release(&furi_hal_i2c_handle_external);
            return;
        }
    }
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    tumomodule_result(
        result,
        compatible_device_seen ? TumoModuleProbeError : TumoModuleProbeMissing,
        compatible_device_seen ? "BME280 sample failed" : "No BME280 at 0x76/0x77");
}

void tumomodule_driver_probe(
    const TumoModuleManifest* manifest,
    TumoModuleDriverContext* context,
    TumoModuleProbeResult* result) {
    if(result == NULL) return;
    tumomodule_result(result, TumoModuleProbeError, "Invalid adapter request");
    if(manifest == NULL || context == NULL) return;
    if(manifest->adapter == TumoModuleAdapterBme280I2c) {
        tumomodule_probe_bme280(manifest, result);
    } else if(manifest->adapter == TumoModuleAdapterTumoVgmUart) {
        tumomodule_probe_vgm(manifest, context, result);
    }
}
