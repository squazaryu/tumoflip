#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SENSOR_CARDS 8U
#define SENSOR_HISTORY 32U
enum {SensorTemperature = 1, SensorHumidity = 2, SensorBattery = 4};
typedef struct {
    uint32_t timestamp;
    int16_t temperature;
    uint8_t humidity;
    uint8_t battery_low;
    uint8_t fields;
} SensorPoint;
typedef struct {
    char protocol[32];
    uint32_t id;
    uint32_t frequency;
    uint32_t frames;
    uint8_t channel;
    uint8_t head;
    uint8_t count;
    SensorPoint history[SENSOR_HISTORY];
} SensorCard;
typedef struct {
    uint32_t version;
    uint32_t rejected;
    uint32_t full;
    uint8_t count;
    SensorCard cards[SENSOR_CARDS];
} SensorCards;

void sensor_cards_init(SensorCards* cards);
bool sensor_cards_valid(const SensorCards* cards);
bool sensor_cards_add(SensorCards* cards, const char* protocol, uint32_t id, uint8_t channel,
                      uint32_t frequency, const SensorPoint* point);
const SensorPoint* sensor_card_point(const SensorCard* card, size_t index);
bool sensor_card_row(const SensorCard* card, size_t index, char* row, size_t capacity);
