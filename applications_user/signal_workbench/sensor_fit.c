#include "sensor_fit.h"
#include <string.h>

static bool sensor_extract(const SensorObservation* s, unsigned start, unsigned width, bool little, bool sign, int32_t* value) {
    uint32_t raw = 0;
    for(unsigned i = start; i < start + width; i++) {
        if(!(s->known[i / 8] & (1U << (7 - i % 8)))) return false;
        raw = (raw << 1) | ((s->bits[i / 8] >> (7 - i % 8)) & 1);
    }
    if(little) raw = ((raw & 255) << 8) | (raw >> 8);
    *value = sign && (raw & (1U << (width - 1))) ? (int32_t)raw - (int32_t)(1U << width) : (int32_t)raw;
    return true;
}
static unsigned sensor_rank(const SensorCandidate* c) {
    // Ranking uses training properties ONLY. The held-out sample never selects a hypothesis.
    return (c->offset10 == 0 ? 8 : 0) + (c->start % 8 == 0 ? 4 : 0) +
        (c->width == 8 || c->width == 16 ? 2 : 0) + (c->scale10 == 1 ? 1 : 0);
}
static void sensor_add(SensorFitResult* r, const SensorCandidate* c) {
    r->total++;
    unsigned index = 0;
    while(index < r->count && sensor_rank(&r->candidates[index]) >= sensor_rank(c)) index++;
    if(index >= SENSOR_MAX_CANDIDATES) return;
    if(r->count < SENSOR_MAX_CANDIDATES) r->count++;
    for(unsigned i = r->count - 1; i > index; i--) r->candidates[i] = r->candidates[i - 1];
    r->candidates[index] = *c;
}
bool sensor_fit(const SensorObservation samples[SENSOR_SAMPLE_COUNT], uint8_t bits, SensorFitResult* r) {
    if(!r) return false;
    memset(r, 0, sizeof(*r));
    if(!samples || bits < 4 || bits > SENSOR_MAX_BITS) return false;
    for(unsigned i = 0; i < SENSOR_SAMPLE_COUNT; i++) if(samples[i].measured10 < -1000000 || samples[i].measured10 > 1000000) return false;
    if(samples[0].measured10 == samples[1].measured10 || samples[0].measured10 == samples[2].measured10 || samples[1].measured10 == samples[2].measured10) return false;
    const uint8_t scales[] = {1, 10, 100};
    for(unsigned start = 0; start < bits; start++) for(unsigned width = 4; width <= 16 && start + width <= bits; width++)
    for(unsigned little = 0; little < (width == 16 && start % 8 == 0 ? 2U : 1U); little++) for(unsigned sign = 0; sign < 2; sign++) {
        int32_t raw[4]; bool known = true;
        for(unsigned i = 0; i < 4; i++) if(!sensor_extract(&samples[i], start, width, little, sign, &raw[i])) known = false;
        if(!known || raw[0] == raw[1] || raw[0] == raw[2] || raw[1] == raw[2]) continue;
        for(unsigned scale = 0; scale < sizeof(scales); scale++) {
            int32_t offset = samples[0].measured10 - raw[0] * scales[scale];
            if(raw[1] * scales[scale] + offset != samples[1].measured10 || raw[2] * scales[scale] + offset != samples[2].measured10) continue;
            SensorCandidate c = {.start=start,.width=width,.signed_value=sign,.little_endian=little,.scale10=scales[scale],.offset10=offset};
            c.predicted10 = raw[3] * c.scale10 + offset;
            c.holdout_match = c.predicted10 == samples[3].measured10;
            sensor_add(r, &c);
        }
    }
    return r->count != 0;
}
