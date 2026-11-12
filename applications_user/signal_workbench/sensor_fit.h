#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define SENSOR_SAMPLE_COUNT   4U
#define SENSOR_MAX_BITS       96U
#define SENSOR_MAX_CANDIDATES 16U
typedef struct {
    uint8_t bits[12];
    uint8_t known[12];
    int32_t measured10;
} SensorObservation;
typedef struct {
    uint8_t start;
    uint8_t width;
    bool signed_value;
    bool little_endian;
    uint8_t scale10;
    int32_t offset10;
    int32_t predicted10;
    bool holdout_match;
} SensorCandidate;
typedef struct {
    SensorCandidate candidates[SENSOR_MAX_CANDIDATES];
    uint32_t total;
    uint8_t count;
} SensorFitResult;
bool sensor_fit(
    const SensorObservation samples[SENSOR_SAMPLE_COUNT],
    uint8_t bits,
    SensorFitResult* result);
bool sensor_candidate_text(
    const SensorCandidate* candidate,
    unsigned number,
    int32_t measured10,
    char* output,
    size_t size);
