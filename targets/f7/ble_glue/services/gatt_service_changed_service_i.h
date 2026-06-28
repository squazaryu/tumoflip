#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BleServiceGattServiceChanged BleServiceGattServiceChanged;

BleServiceGattServiceChanged* ble_svc_gatt_service_changed_start(void);

void ble_svc_gatt_service_changed_stop(BleServiceGattServiceChanged* service);

void ble_svc_gatt_service_changed_mark_dirty(BleServiceGattServiceChanged* service);

#ifdef __cplusplus
}
#endif
