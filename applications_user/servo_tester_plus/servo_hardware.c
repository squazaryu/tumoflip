#include "servo_hardware.h"
#include "servo_model.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_pwm.h>
#include <stm32wbxx_ll_tim.h>

void servo_hardware_stop(ServoHardware* hardware) {
    if(!hardware) return;
    if(hardware->pwm_owned) {
        if(furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
            furi_hal_pwm_stop(FuriHalPwmOutputIdTim1PA7);
        }
        hardware->pwm_owned = false;
    }
    if(hardware->otg_owned) {
        furi_hal_power_disable_otg();
        hardware->otg_owned = false;
    }
    if(hardware->broker) {
        subghz_radio_broker_set_state(
            hardware->broker, &hardware->lease, SubGhzRadioBrokerStateCleaningUp);
        subghz_radio_broker_release(hardware->broker, &hardware->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        hardware->broker = NULL;
    }
}

bool servo_hardware_apply(ServoHardware* hardware, uint16_t pulse) {
    if(!hardware || !hardware->pwm_owned) return false;
    uint32_t compare;
    bool ready;
    FURI_CRITICAL_ENTER();
    ready = furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7) &&
            servo_compare_ticks(LL_TIM_GetAutoReload(TIM1) + 1U, pulse, &compare);
    if(ready) LL_TIM_OC_SetCompareCH1(TIM1, compare);
    FURI_CRITICAL_EXIT();
    if(!ready) servo_hardware_stop(hardware);
    return ready;
}

ServoHardwareStatus servo_hardware_start(ServoHardware* hardware, uint16_t pulse, bool gpio_power) {
    if(!hardware || pulse < 500U || pulse > 2500U) return ServoHardwareInvalid;
    if(hardware->broker || hardware->pwm_owned) return ServoHardwareBusy;
    SubGhzRadioBroker* broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    if(!subghz_radio_broker_acquire(broker, "servo_tester", 100U, &hardware->lease)) {
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        return ServoHardwareBusy;
    }
    hardware->broker = broker;
    // TIM1 is also used by other peripherals. Never reconfigure an enabled bus.
    FURI_CRITICAL_ENTER();
    if(!furi_hal_pwm_is_running(FuriHalPwmOutputIdTim1PA7)) {
        furi_hal_pwm_start(FuriHalPwmOutputIdTim1PA7, 50U, 0U);
        // PA7 is TIM1's complementary CH1N output. Invert its polarity so CCR
        // describes the HIGH pulse, not the remainder of the 20ms frame.
        LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1N, LL_TIM_OCPOLARITY_LOW);
        hardware->pwm_owned = true;
    }
    FURI_CRITICAL_EXIT();
    if(!hardware->pwm_owned) {
        servo_hardware_stop(hardware);
        return ServoHardwareBusy;
    }
    if(gpio_power && !furi_hal_power_is_otg_enabled()) {
        if(!furi_hal_power_enable_otg()) {
            servo_hardware_stop(hardware);
            return ServoHardwarePowerError;
        }
        hardware->otg_owned = true;
    }
    if(!servo_hardware_apply(hardware, pulse)) return ServoHardwareInvalid;
    return ServoHardwareOk;
}
