#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t frequency_index;
    size_t preset_hopper_index;
    bool frequency_changed;
    bool preset_changed;
    bool preset_wrapped;
} SubGhzHopperPlan;

static inline bool subghz_hopper_plan_next(
    size_t* frequency_index,
    size_t* preset_hopper_index,
    bool hop_frequency,
    bool hop_preset,
    size_t frequency_count,
    size_t preset_count,
    SubGhzHopperPlan* plan) {
    if(!hop_frequency && !hop_preset) return false;
    if(hop_frequency && (!frequency_index || frequency_count == 0U)) return false;
    if(hop_preset && (!preset_hopper_index || preset_count == 0U)) return false;

    SubGhzHopperPlan next = {
        .frequency_index = hop_frequency ? *frequency_index : 0U,
        .preset_hopper_index = hop_preset ? *preset_hopper_index : 0U,
        .frequency_changed = false,
        .preset_changed = false,
        .preset_wrapped = false,
    };

    if(hop_preset) {
        next.preset_hopper_index++;
        next.preset_changed = true;
        if(next.preset_hopper_index >= preset_count) {
            next.preset_hopper_index = 0U;
            next.preset_wrapped = true;
        }
        *preset_hopper_index = next.preset_hopper_index;
    }

    if(hop_frequency && (!hop_preset || next.preset_wrapped)) {
        next.frequency_index = (*frequency_index + 1U) % frequency_count;
        next.frequency_changed = true;
        *frequency_index = next.frequency_index;
    }

    if(plan) {
        if(hop_frequency) {
            next.frequency_index = *frequency_index;
        }
        if(hop_preset) {
            next.preset_hopper_index = *preset_hopper_index;
        }
        *plan = next;
    }

    return true;
}
