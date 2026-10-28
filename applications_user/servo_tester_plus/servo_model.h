#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ServoManual,
    ServoCenter,
    ServoSweep,
} ServoMode;

typedef struct {
    uint16_t minimum_us;
    uint16_t maximum_us;
    uint16_t pulse_us;
    ServoMode mode;
    uint32_t last_tick;
    bool armed;
    bool rising;
} ServoModel;

void servo_model_init(ServoModel* model);
bool servo_model_limits(ServoModel* model, uint16_t minimum, uint16_t maximum);
bool servo_model_mode(ServoModel* model, ServoMode mode, uint32_t now);
void servo_model_nudge(ServoModel* model, int direction);
void servo_model_arm(ServoModel* model, uint32_t now);
void servo_model_stop(ServoModel* model);
bool servo_model_tick(ServoModel* model, uint32_t now);
bool servo_compare_ticks(uint32_t period_ticks, uint16_t pulse_us, uint32_t* compare);
