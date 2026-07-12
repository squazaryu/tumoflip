#pragma once

#include "tumoscope_capture.h"

typedef struct {
    uint8_t* storage;
    size_t capacity;
    TumoScopeTrigger trigger;
    uint8_t pretrigger_percent;
    size_t total_samples;
    size_t write_index;
    size_t post_remaining;
    size_t trigger_index;
    uint8_t previous_sample;
    bool previous_valid;
    bool triggered;
    bool complete;
} TumoScopeRing;

void tumoscope_ring_init(
    TumoScopeRing* ring,
    uint8_t* storage,
    size_t capacity,
    TumoScopeTrigger trigger,
    uint8_t pretrigger_percent);

bool tumoscope_ring_push(TumoScopeRing* ring, uint8_t sample);
size_t tumoscope_ring_progress(const TumoScopeRing* ring);
size_t tumoscope_ring_count(const TumoScopeRing* ring);
bool tumoscope_ring_copy(const TumoScopeRing* ring, uint8_t* output, size_t capacity);
