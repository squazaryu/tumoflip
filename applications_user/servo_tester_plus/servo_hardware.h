#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

typedef enum {
    ServoHardwareOk,
    ServoHardwareBusy,
    ServoHardwarePowerError,
    ServoHardwareInvalid,
} ServoHardwareStatus;

typedef struct {
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    bool pwm_owned;
    bool otg_owned;
} ServoHardware;

ServoHardwareStatus servo_hardware_start(ServoHardware* hardware, uint16_t pulse, bool gpio_power);
bool servo_hardware_apply(ServoHardware* hardware, uint16_t pulse);
void servo_hardware_stop(ServoHardware* hardware);
