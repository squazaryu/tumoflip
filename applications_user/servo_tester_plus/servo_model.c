#include "servo_model.h"

static bool servo_limits_valid(uint16_t minimum, uint16_t maximum) {
    return minimum >= 500U && maximum <= 2500U && maximum > minimum &&
           maximum - minimum >= 100U;
}

void servo_model_init(ServoModel* model) {
    if(!model) return;
    *model = (ServoModel){
        .minimum_us = 1000U,
        .maximum_us = 2000U,
        .pulse_us = 1500U,
        .mode = ServoManual,
        .rising = true,
    };
}

bool servo_model_limits(ServoModel* model, uint16_t minimum, uint16_t maximum) {
    if(!model || model->armed || !servo_limits_valid(minimum, maximum)) return false;
    model->minimum_us = minimum;
    model->maximum_us = maximum;
    if(model->pulse_us < minimum) model->pulse_us = minimum;
    if(model->pulse_us > maximum) model->pulse_us = maximum;
    if(model->mode == ServoCenter) model->pulse_us = minimum + (maximum - minimum) / 2U;
    return true;
}

void servo_model_stop(ServoModel* model) {
    if(model) model->armed = false;
}

bool servo_model_mode(ServoModel* model, ServoMode mode, uint32_t now) {
    if(!model || mode < ServoManual || mode > ServoSweep) return false;
    // Selecting another mode never silently restarts or moves an actuator.
    servo_model_stop(model);
    model->mode = mode;
    model->last_tick = now;
    model->rising = true;
    if(mode == ServoCenter) {
        model->pulse_us = model->minimum_us + (model->maximum_us - model->minimum_us) / 2U;
    }
    return true;
}

void servo_model_nudge(ServoModel* model, int direction) {
    if(!model || !direction) return;
    if(model->mode != ServoManual) {
        servo_model_stop(model);
        model->mode = ServoManual;
    }
    int32_t next = (int32_t)model->pulse_us + (direction > 0 ? 10 : -10);
    if(next < model->minimum_us) next = model->minimum_us;
    if(next > model->maximum_us) next = model->maximum_us;
    model->pulse_us = (uint16_t)next;
}

void servo_model_arm(ServoModel* model, uint32_t now) {
    if(!model || !servo_limits_valid(model->minimum_us, model->maximum_us) ||
       model->pulse_us < model->minimum_us || model->pulse_us > model->maximum_us) {
        return;
    }
    model->last_tick = now;
    model->armed = true;
}

bool servo_model_tick(ServoModel* model, uint32_t now) {
    if(!model || !model->armed) return false;
    const uint32_t elapsed = now - model->last_tick;
    if(elapsed > 2000U) {
        // Detected on the next UI tick, not an independent hardware watchdog.
        servo_model_stop(model);
        return true;
    }
    if(model->mode != ServoSweep) {
        model->last_tick = now;
        return false;
    }
    if(elapsed < 50U) return false;
    unsigned steps = elapsed / 50U;
    model->last_tick += steps * 50U;
    while(steps--) {
        if(model->rising) {
            const uint32_t next = (uint32_t)model->pulse_us + 20U;
            model->pulse_us = next > model->maximum_us ? model->maximum_us : (uint16_t)next;
            if(model->pulse_us == model->maximum_us) model->rising = false;
        } else {
            const int32_t next = (int32_t)model->pulse_us - 20;
            model->pulse_us = next < model->minimum_us ? model->minimum_us : (uint16_t)next;
            if(model->pulse_us == model->minimum_us) model->rising = true;
        }
    }
    return true;
}

bool servo_compare_ticks(uint32_t period_ticks, uint16_t pulse_us, uint32_t* compare) {
    if(!compare || !period_ticks || pulse_us < 500U || pulse_us > 2500U) return false;
    const uint32_t result = (uint32_t)(((uint64_t)period_ticks * pulse_us) / 20000U);
    if(!result || result >= period_ticks) return false;
    *compare = result;
    return true;
}
