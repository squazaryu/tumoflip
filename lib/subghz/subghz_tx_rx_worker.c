#include "subghz_tx_rx_worker.h"

#include <furi.h>
#include <applications/services/subghz_radio_broker/subghz_radio_broker.h>
#include "packet_config.h"
#include <string.h>

#define TAG "SubGhzTxRxWorker"

#define SUBGHZ_TXRX_WORKER_BUF_SIZE      2048
//you can not set more than 62 because it will not fit into the FIFO CC1101
#define SUBGHZ_TXRX_WORKER_MAX_TXRX_SIZE 60

#define SUBGHZ_TXRX_WORKER_TIMEOUT_READ_WRITE_BUF 40

struct SubGhzTxRxWorker {
    FuriThread* thread;
    FuriStreamBuffer* stream_tx;
    FuriStreamBuffer* stream_rx;

    volatile bool worker_running;
    bool thread_started;
    bool device_begun;
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    FuriHalSubGhzPreset preset;
    uint8_t preset_data[SUBGHZ_PACKET_PRESET_MAX];
    size_t preset_size;
    uint8_t channel;

    SubGhzTxRxWorkerStatus status;

    uint32_t frequency;
    const SubGhzDevice* device;
    const GpioPin* device_data_gpio;

    SubGhzTxRxWorkerCallbackHaveRead callback_have_read;
    void* context_have_read;
};

bool subghz_tx_rx_worker_write(SubGhzTxRxWorker* instance, uint8_t* data, size_t size) {
    furi_check(instance);
    if(!instance->worker_running || !data) return false;
    bool ret = false;
    size_t stream_tx_free_byte = furi_stream_buffer_spaces_available(instance->stream_tx);
    if(size && (stream_tx_free_byte >= size)) {
        if(furi_stream_buffer_send(
               instance->stream_tx, data, size, SUBGHZ_TXRX_WORKER_TIMEOUT_READ_WRITE_BUF) ==
           size) {
            ret = true;
        }
    }
    return ret;
}

size_t subghz_tx_rx_worker_available(SubGhzTxRxWorker* instance) {
    furi_check(instance);
    return furi_stream_buffer_bytes_available(instance->stream_rx);
}

size_t subghz_tx_rx_worker_read(SubGhzTxRxWorker* instance, uint8_t* data, size_t size) {
    furi_check(instance);
    return furi_stream_buffer_receive(instance->stream_rx, data, size, 0);
}

void subghz_tx_rx_worker_set_callback_have_read(
    SubGhzTxRxWorker* instance,
    SubGhzTxRxWorkerCallbackHaveRead callback,
    void* context) {
    furi_check(instance);
    furi_check(callback);
    furi_check(context);
    instance->callback_have_read = callback;
    instance->context_have_read = context;
}

bool subghz_tx_rx_worker_rx(SubGhzTxRxWorker* instance, uint8_t* data, uint8_t* size) {
    uint8_t timeout = 100;
    bool ret = false;
    if(instance->status != SubGhzTxRxWorkerStatusRx) {
        subghz_devices_set_rx(instance->device);
        instance->status = SubGhzTxRxWorkerStatusRx;
        furi_delay_tick(1);
    }
    //waiting for reception to complete
    while(furi_hal_gpio_read(instance->device_data_gpio)) {
        furi_delay_tick(1);
        if(!--timeout) {
            FURI_LOG_W(TAG, "RX cc1101_g0 timeout");
            subghz_devices_flush_rx(instance->device);
            subghz_devices_set_rx(instance->device);
            break;
        }
    }

    if(subghz_devices_rx_pipe_not_empty(instance->device)) {
        FURI_LOG_I(
            TAG,
            "RSSI: %03.1fdbm LQI: %d",
            (double)subghz_devices_get_rssi(instance->device),
            subghz_devices_get_lqi(instance->device));
        if(subghz_devices_is_rx_data_crc_valid(instance->device)) {
            subghz_devices_read_packet(instance->device, data, size);
            ret = true;
        }
        subghz_devices_flush_rx(instance->device);
        subghz_devices_set_rx(instance->device);
    }
    return ret;
}

void subghz_tx_rx_worker_tx(SubGhzTxRxWorker* instance, uint8_t* data, size_t size) {
    uint8_t timeout = 200;
    if(instance->status != SubGhzTxRxWorkerStatusIDLE) {
        subghz_devices_idle(instance->device);
    }
    subghz_devices_write_packet(instance->device, data, size);
    if(!subghz_devices_set_tx(instance->device)) {
        subghz_devices_flush_tx(instance->device);
        return;
    }
    instance->status = SubGhzTxRxWorkerStatusTx;
    while(!furi_hal_gpio_read(
        instance->device_data_gpio)) { // Wait for GDO0 to be set -> sync transmitted
        furi_delay_tick(1);
        if(!--timeout) {
            FURI_LOG_W(TAG, "TX !cc1101_g0 timeout");
            break;
        }
    }
    while(furi_hal_gpio_read(
        instance->device_data_gpio)) { // Wait for GDO0 to be cleared -> end of packet
        furi_delay_tick(1);
        if(!--timeout) {
            FURI_LOG_W(TAG, "TX cc1101_g0 timeout");
            break;
        }
    }
    subghz_devices_idle(instance->device);
    instance->status = SubGhzTxRxWorkerStatusIDLE;
}
/** Worker thread
 * 
 * @param context 
 * @return exit code 
 */
static void subghz_tx_rx_worker_release(SubGhzTxRxWorker* instance) {
    if(instance->device_begun) {
        subghz_devices_sleep(instance->device);
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }
    if(instance->broker) {
        if(instance->lease.token) subghz_radio_broker_release(instance->broker, &instance->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        instance->broker = NULL;
    }
}

static bool subghz_tx_rx_worker_prepare(
    SubGhzTxRxWorker* instance, const SubGhzDevice* device, uint32_t frequency) {
    instance->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!instance->broker) return false;
    if(!subghz_radio_broker_acquire(
           instance->broker, "packet-worker", furi_ms_to_ticks(500), &instance->lease)) {
        subghz_tx_rx_worker_release(instance);
        return false;
    }
    instance->device = device;
    if(device->interconnect->begin && !subghz_devices_begin(device)) {
        subghz_devices_end(device);
        subghz_tx_rx_worker_release(instance);
        return false;
    }
    instance->device_begun = true;
    instance->device_data_gpio = subghz_devices_get_data_gpio(device);
    if(!instance->device_data_gpio) {
        subghz_tx_rx_worker_release(instance);
        return false;
    }
    subghz_devices_reset(device);
    subghz_devices_idle(device);
    subghz_devices_load_preset(device, instance->preset,
                              instance->preset_size ? instance->preset_data : NULL);
    if(!subghz_devices_set_frequency(device, frequency) ||
       !subghz_devices_set_channel(device, instance->channel)) {
        subghz_tx_rx_worker_release(instance);
        return false;
    }
    furi_hal_gpio_init(instance->device_data_gpio, GpioModeInput, GpioPullNo, GpioSpeedLow);
    subghz_devices_flush_rx(device);
    instance->frequency = frequency;
    subghz_radio_broker_set_state(instance->broker, &instance->lease, SubGhzRadioBrokerStateInitialized);
    return true;
}

static int32_t subghz_tx_rx_worker_thread(void* context) {
    SubGhzTxRxWorker* instance = context;
    furi_check(instance->device);
    FURI_LOG_I(TAG, "Worker start");

    // The low-level FIFO reader can return up to 64 bytes, even for a bad length.
    uint8_t data[64] = {0};
    size_t size_tx = 0;
    uint8_t size_rx[1] = {0};
    uint8_t timeout_tx = 0;
    bool callback_rx = false;

    while(instance->worker_running) {
        if(!subghz_devices_is_connect(instance->device)) break;
        //transmit
        size_tx = furi_stream_buffer_bytes_available(instance->stream_tx);
        if(size_tx > 0 && !timeout_tx) {
            timeout_tx = 10; //20ms
            if(size_tx > SUBGHZ_TXRX_WORKER_MAX_TXRX_SIZE) {
                furi_stream_buffer_receive(
                    instance->stream_tx,
                    &data,
                    SUBGHZ_TXRX_WORKER_MAX_TXRX_SIZE,
                    SUBGHZ_TXRX_WORKER_TIMEOUT_READ_WRITE_BUF);
                subghz_tx_rx_worker_tx(instance, data, SUBGHZ_TXRX_WORKER_MAX_TXRX_SIZE);
            } else {
                //TODO FL-3554: checking that it managed to write all the data to the TX buffer
                furi_stream_buffer_receive(
                    instance->stream_tx, &data, size_tx, SUBGHZ_TXRX_WORKER_TIMEOUT_READ_WRITE_BUF);
                subghz_tx_rx_worker_tx(instance, data, size_tx);
            }
        } else {
            //receive
            if(subghz_tx_rx_worker_rx(instance, data, size_rx) &&
               size_rx[0] <= SUBGHZ_TXRX_WORKER_MAX_TXRX_SIZE) {
                if(furi_stream_buffer_spaces_available(instance->stream_rx) >= size_rx[0]) {
                    if(instance->callback_have_read &&
                       furi_stream_buffer_bytes_available(instance->stream_rx) == 0) {
                        callback_rx = true;
                    }
                    //TODO FL-3554: checking that it managed to write all the data to the RX buffer
                    furi_stream_buffer_send(
                        instance->stream_rx,
                        &data,
                        size_rx[0],
                        SUBGHZ_TXRX_WORKER_TIMEOUT_READ_WRITE_BUF);
                    if(callback_rx) {
                        instance->callback_have_read(instance->context_have_read);
                        callback_rx = false;
                    }
                } else {
                    //TODO FL-3555: RX buffer overflow
                }
            }
        }

        if(timeout_tx) timeout_tx--;
        furi_delay_tick(1);
    }

    subghz_tx_rx_worker_release(instance);
    instance->worker_running = false;

    FURI_LOG_I(TAG, "Worker stop");
    return 0;
}

SubGhzTxRxWorker* subghz_tx_rx_worker_alloc(void) {
    SubGhzTxRxWorker* instance = calloc(1, sizeof(SubGhzTxRxWorker));

    instance->thread =
        furi_thread_alloc_ex("SubGhzTxRxWorker", 2048, subghz_tx_rx_worker_thread, instance);
    instance->stream_tx =
        furi_stream_buffer_alloc(sizeof(uint8_t) * SUBGHZ_TXRX_WORKER_BUF_SIZE, sizeof(uint8_t));
    instance->stream_rx =
        furi_stream_buffer_alloc(sizeof(uint8_t) * SUBGHZ_TXRX_WORKER_BUF_SIZE, sizeof(uint8_t));

    instance->status = SubGhzTxRxWorkerStatusIDLE;
    instance->preset = FuriHalSubGhzPresetGFSK9_99KbAsync;

    return instance;
}

void subghz_tx_rx_worker_free(SubGhzTxRxWorker* instance) {
    furi_check(instance);
    if(instance->thread_started) subghz_tx_rx_worker_stop(instance);
    furi_stream_buffer_free(instance->stream_tx);
    furi_stream_buffer_free(instance->stream_rx);
    furi_thread_free(instance->thread);

    free(instance);
}

bool subghz_tx_rx_worker_start(
    SubGhzTxRxWorker* instance,
    const SubGhzDevice* device,
    uint32_t frequency) {
    furi_check(instance);
    if(instance->thread_started || !device || !furi_hal_subghz_is_tx_allowed(frequency) ||
       !subghz_devices_is_frequency_valid(device, frequency)) return false;
    furi_stream_buffer_reset(instance->stream_tx);
    furi_stream_buffer_reset(instance->stream_rx);

    if(!subghz_tx_rx_worker_prepare(instance, device, frequency)) return false;
    instance->worker_running = true;
    instance->thread_started = true;
    furi_thread_start(instance->thread);

    return true;
}

bool subghz_tx_rx_worker_set_preset(
    SubGhzTxRxWorker* instance, FuriHalSubGhzPreset preset, const uint8_t* data, size_t size) {
    if(!instance || instance->thread_started) return false;
    if(preset == FuriHalSubGhzPresetCustom) {
        if(!subghz_packet_preset_is_valid(data, size)) return false;
    } else if(preset != FuriHalSubGhzPresetGFSK9_99KbAsync || data || size) return false;
    if(size) memcpy(instance->preset_data, data, size);
    instance->preset_size = size;
    instance->preset = preset;
    return true;
}

bool subghz_tx_rx_worker_set_channel(SubGhzTxRxWorker* instance, uint8_t channel) {
    if(!instance || instance->thread_started) return false;
    instance->channel = channel;
    return true;
}

void subghz_tx_rx_worker_stop(SubGhzTxRxWorker* instance) {
    furi_check(instance);
    furi_check(instance->thread_started);

    instance->worker_running = false;

    furi_thread_join(instance->thread);
    instance->thread_started = false;
}

bool subghz_tx_rx_worker_is_running(SubGhzTxRxWorker* instance) {
    furi_check(instance);
    return instance->worker_running;
}
