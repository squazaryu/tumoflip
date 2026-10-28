#include "sensor_cards.h"
#include <stdio.h>
#include <string.h>

static bool sensor_protocol_valid(const char* name) {
    if(!name || !name[0] || !memchr(name, 0, 32U) ||
       !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) return false;
    for(const char* p = name; *p; p++)
        if(!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
             (*p >= '0' && *p <= '9') || *p == ' ' || *p == '-' || *p == '_' || *p == '+')) return false;
    return true;
}

static bool sensor_point_valid(const SensorPoint* point) {
    return point && point->fields && !(point->fields & ~7U) &&
           (!(point->fields & SensorTemperature) ||
            (point->temperature >= -800 && point->temperature <= 1000)) &&
           (!(point->fields & SensorHumidity) || point->humidity <= 100U) &&
           (!(point->fields & SensorBattery) || point->battery_low <= 1U);
}

void sensor_cards_init(SensorCards* cards) {
    if(!cards) return;
    memset(cards, 0, sizeof(*cards));
    cards->version = 1;
}

const SensorPoint* sensor_card_point(const SensorCard* card, size_t index) {
    if(!card || card->count > SENSOR_HISTORY || card->head >= SENSOR_HISTORY || index >= card->count) return NULL;
    return &card->history[(card->head + SENSOR_HISTORY - card->count + index) % SENSOR_HISTORY];
}

bool sensor_cards_valid(const SensorCards* cards) {
    if(!cards || cards->version != 1U || cards->count > SENSOR_CARDS) return false;
    for(unsigned i = 0; i < cards->count; i++) {
        const SensorCard* card = &cards->cards[i];
        if(!sensor_protocol_valid(card->protocol) || card->id == UINT32_MAX ||
           card->frequency < 300000000U || card->frequency > 928000000U ||
           !card->count || card->count > SENSOR_HISTORY || card->head >= SENSOR_HISTORY) return false;
        for(unsigned j = 0; j < card->count; j++)
            if(!sensor_point_valid(sensor_card_point(card, j))) return false;
    }
    return true;
}

bool sensor_cards_add(SensorCards* cards, const char* protocol, uint32_t id, uint8_t channel,
                      uint32_t frequency, const SensorPoint* point) {
    if(!cards || cards->count > SENSOR_CARDS) return false;
    if(!sensor_protocol_valid(protocol) || id == UINT32_MAX || frequency < 300000000U ||
       frequency > 928000000U || !sensor_point_valid(point)) {
        if(cards->rejected != UINT32_MAX) cards->rejected++;
        return false;
    }
    unsigned index = 0;
    while(index < cards->count) {
        const SensorCard* card = &cards->cards[index];
        if(card->id == id && card->channel == channel && card->frequency == frequency &&
           !strcmp(card->protocol, protocol)) break;
        index++;
    }
    if(index == cards->count) {
        if(index == SENSOR_CARDS) {
            if(cards->full != UINT32_MAX) cards->full++;
            return false;
        }
        SensorCard* card = &cards->cards[cards->count++];
        memset(card, 0, sizeof(*card));
        memcpy(card->protocol, protocol, strlen(protocol) + 1U);
        card->id = id;
        card->channel = channel;
        card->frequency = frequency;
    }
    SensorCard* card = &cards->cards[index];
    if(card->frames != UINT32_MAX) card->frames++;
    const SensorPoint* last = card->count ? sensor_card_point(card, card->count - 1U) : NULL;
    if(last && last->timestamp == point->timestamp && last->temperature == point->temperature &&
       last->humidity == point->humidity && last->battery_low == point->battery_low &&
       last->fields == point->fields) return true;
    card->history[card->head] = *point;
    card->head = (card->head + 1U) % SENSOR_HISTORY;
    if(card->count < SENSOR_HISTORY) card->count++;
    return true;
}

bool sensor_card_row(const SensorCard* card, size_t index, char* row, size_t capacity) {
    const SensorPoint* point = sensor_card_point(card, index);
    if(!point || !row || !sensor_protocol_valid(card->protocol)) return false;
    char temperature[8] = "", humidity[4] = "", battery[2] = "";
    if(point->fields & SensorTemperature) snprintf(temperature, sizeof(temperature), "%d", point->temperature);
    if(point->fields & SensorHumidity) snprintf(humidity, sizeof(humidity), "%u", point->humidity);
    if(point->fields & SensorBattery) snprintf(battery, sizeof(battery), "%u", point->battery_low);
    const int length = snprintf(row, capacity, "%s,%lu,%u,%lu,%lu,%s,%s,%s,%lu\n", card->protocol,
        (unsigned long)card->id, card->channel, (unsigned long)card->frequency,
        (unsigned long)point->timestamp, temperature, humidity, battery, (unsigned long)card->frames);
    return length > 0 && (size_t)length < capacity;
}
