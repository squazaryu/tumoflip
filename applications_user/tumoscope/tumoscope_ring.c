#include "tumoscope_ring.h"

#include <string.h>

static bool
    tumoscope_ring_trigger_matches(TumoScopeTrigger trigger, uint8_t previous, uint8_t current) {
    if(trigger == TumoScopeTriggerAuto) return true;

    uint8_t channel = 0U;
    bool rising = true;
    bool level = false;
    switch(trigger) {
    case TumoScopeTriggerPc0Rising:
        channel = 0U;
        break;
    case TumoScopeTriggerPc0Falling:
        channel = 0U;
        rising = false;
        break;
    case TumoScopeTriggerPc1Rising:
        channel = 1U;
        break;
    case TumoScopeTriggerPc1Falling:
        channel = 1U;
        rising = false;
        break;
    case TumoScopeTriggerPc3Rising:
        channel = 2U;
        break;
    case TumoScopeTriggerPc3Falling:
        channel = 2U;
        rising = false;
        break;
    case TumoScopeTriggerPc0High:
        channel = 0U;
        level = true;
        break;
    case TumoScopeTriggerPc0Low:
        channel = 0U;
        rising = false;
        level = true;
        break;
    case TumoScopeTriggerPc1High:
        channel = 1U;
        level = true;
        break;
    case TumoScopeTriggerPc1Low:
        channel = 1U;
        rising = false;
        level = true;
        break;
    case TumoScopeTriggerPc3High:
        channel = 2U;
        level = true;
        break;
    case TumoScopeTriggerPc3Low:
        channel = 2U;
        rising = false;
        level = true;
        break;
    case TumoScopeTriggerAuto:
        return true;
    }

    const bool previous_level = (previous & (1U << channel)) != 0U;
    const bool current_level = (current & (1U << channel)) != 0U;
    if(level) return current_level == rising;
    return rising ? (!previous_level && current_level) : (previous_level && !current_level);
}

static bool tumoscope_ring_trigger_needs_previous(TumoScopeTrigger trigger) {
    return trigger >= TumoScopeTriggerPc0Rising && trigger <= TumoScopeTriggerPc3Falling;
}

void tumoscope_ring_init(
    TumoScopeRing* ring,
    uint8_t* storage,
    size_t capacity,
    TumoScopeTrigger trigger,
    uint8_t pretrigger_percent) {
    memset(ring, 0, sizeof(*ring));
    ring->storage = storage;
    ring->capacity = capacity;
    ring->trigger = trigger;
    ring->pretrigger_percent = pretrigger_percent;
    memset(storage, 0, capacity);
}

bool tumoscope_ring_push(TumoScopeRing* ring, uint8_t sample) {
    if(!ring || !ring->storage || !ring->capacity || ring->complete) return ring && ring->complete;

    const size_t pretrigger = ring->trigger == TumoScopeTriggerAuto ?
                                  0U :
                                  ring->capacity * ring->pretrigger_percent / 100U;
    ring->storage[ring->write_index] = sample;
    ring->write_index = (ring->write_index + 1U) % ring->capacity;
    ring->total_samples++;

    if(!ring->triggered) {
        const bool enough_history = ring->total_samples > pretrigger;
        if(ring->trigger == TumoScopeTriggerAuto ||
           (enough_history &&
            (!tumoscope_ring_trigger_needs_previous(ring->trigger) || ring->previous_valid) &&
            tumoscope_ring_trigger_matches(ring->trigger, ring->previous_sample, sample))) {
            ring->triggered = true;
            ring->trigger_index = pretrigger;
            ring->post_remaining = ring->capacity - pretrigger - 1U;
            if(ring->trigger == TumoScopeTriggerAuto) ring->post_remaining = ring->capacity - 1U;
        }
    } else if(ring->post_remaining > 0U) {
        ring->post_remaining--;
    }

    ring->previous_sample = sample;
    ring->previous_valid = true;
    if(ring->triggered && ring->post_remaining == 0U) ring->complete = true;
    return ring->complete;
}

size_t tumoscope_ring_progress(const TumoScopeRing* ring) {
    if(!ring || !ring->capacity) return 0U;
    if(!ring->triggered) {
        const size_t pretrigger = ring->capacity * ring->pretrigger_percent / 100U;
        return ring->total_samples < pretrigger ? ring->total_samples : pretrigger;
    }
    return ring->capacity - ring->post_remaining;
}

size_t tumoscope_ring_count(const TumoScopeRing* ring) {
    if(!ring || !ring->complete) return 0U;
    return ring->total_samples < ring->capacity ? ring->total_samples : ring->capacity;
}

bool tumoscope_ring_copy(const TumoScopeRing* ring, uint8_t* output, size_t capacity) {
    const size_t count = tumoscope_ring_count(ring);
    if(!count || !output || capacity < count) return false;
    const size_t start = ring->write_index % ring->capacity;
    for(size_t index = 0U; index < count; index++) {
        output[index] = ring->storage[(start + index) % ring->capacity];
    }
    return true;
}
