#include "rx_link.h"
#include <string.h>

uint32_t rx_link_read32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void rx_link_write32(uint8_t* p, uint32_t value) {
    for(unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static uint16_t rx_link_crc(const uint8_t* p, size_t length) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for(unsigned bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t rx_link_encode(const RxLinkFrame* frame, uint8_t* output, size_t capacity) {
    const size_t size = (size_t)frame->length + 9;
    if(frame->length > RX_LINK_PAYLOAD_MAX || capacity < size) return 0;
    output[0] = 'T';
    output[1] = 'R';
    output[2] = 1;
    output[3] = frame->type;
    output[4] = (uint8_t)frame->sequence;
    output[5] = (uint8_t)(frame->sequence >> 8);
    output[6] = frame->length;
    memcpy(output + 7, frame->data, frame->length);
    uint16_t crc = rx_link_crc(output + 2, size - 4);
    output[size - 2] = (uint8_t)crc;
    output[size - 1] = (uint8_t)(crc >> 8);
    return size;
}

static void rx_link_reject(RxLinkParser* p, uint8_t byte) {
    if(p->errors < UINT32_MAX) p->errors++;
    p->used = byte == 'T' ? 1 : 0;
    p->buffer[0] = 'T';
    p->expected = 0;
}

bool rx_link_feed(RxLinkParser* p, uint8_t byte, RxLinkFrame* frame) {
    if(!p->used) {
        if(byte == 'T') p->buffer[p->used++] = byte;
        return false;
    }
    if((p->used == 1 && byte != 'R') || (p->used == 2 && byte != 1) ||
       (p->used == 6 && byte > RX_LINK_PAYLOAD_MAX)) {
        rx_link_reject(p, byte);
        return false;
    }
    p->buffer[p->used++] = byte;
    if(p->used == 7) p->expected = (uint16_t)byte + 9;
    if(!p->expected || p->used != p->expected) return false;
    const size_t n = p->expected;
    const uint16_t actual = (uint16_t)p->buffer[n - 2] | ((uint16_t)p->buffer[n - 1] << 8);
    if(actual != rx_link_crc(p->buffer + 2, n - 4)) {
        rx_link_reject(p, byte);
        return false;
    }
    frame->type = p->buffer[3];
    frame->sequence = (uint16_t)p->buffer[4] | ((uint16_t)p->buffer[5] << 8);
    frame->length = p->buffer[6];
    memcpy(frame->data, p->buffer + 7, frame->length);
    p->used = p->expected = 0;
    return true;
}

static uint32_t rx_link_add_saturated(uint32_t a, uint32_t b) {
    return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

static void rx_link_stats_reply(RxLinkBackend* b, RxLinkFrame* reply) {
    RxLinkSnapshot* s = &b->snapshot;
    s->mean = s->count ? (uint32_t)(b->duration_sum / s->count) : 0;
    s->high_permille = b->duration_sum ? (uint32_t)(b->high_sum * 1000 / b->duration_sum) : 0;
    const uint32_t values[] = {b->session, s->count, s->minimum, s->maximum, s->mean,
                              s->high_permille, s->dropped_edges, s->lost_batches,
                              s->rejected_batches};
    reply->type = RxLinkStats;
    reply->sequence = b->expected_sequence;
    reply->length = RX_LINK_STATS_SIZE;
    for(size_t i = 0; i < 9; i++) rx_link_write32(reply->data + 4 * i, values[i]);
    for(size_t i = 0; i < RX_LINK_BINS; i++) rx_link_write32(reply->data + 36 + 4 * i, s->bins[i]);
}

bool rx_link_backend_handle(RxLinkBackend* b, const RxLinkFrame* f, RxLinkFrame* reply) {
    if(f->type == RxLinkHello) {
        if(f->length != 8 || memcmp(f->data, "RXA1", 4)) return false;
        const uint32_t session = rx_link_read32(f->data + 4);
        if(!session) return false;
        if(!b->active || b->session != session) {
            memset(b, 0, sizeof(*b));
            b->session = session;
            b->active = true;
        }
        *reply = *f;
        reply->type = RxLinkReady;
        reply->data[8] = 1; // Module One v6.1 ESP32 profile, not C5.
        reply->length = 9;
        return true;
    }
    if(!b->active || f->length < 4 || rx_link_read32(f->data) != b->session) return false;
    if(f->type == RxLinkQuery && f->length == 4) {
        rx_link_stats_reply(b, reply);
        return true;
    }
    if(f->type == RxLinkStop && f->length == 4) {
        b->active = false;
        return false;
    }
    if(f->type != RxLinkEdges) return false;
    if(f->length < 12 || f->length > RX_LINK_PAYLOAD_MAX || (f->length - 8) % 4) {
        b->snapshot.rejected_batches = rx_link_add_saturated(b->snapshot.rejected_batches, 1);
        return false;
    }
    for(unsigned i = 8; i < f->length; i += 4) {
        uint32_t duration = rx_link_read32(f->data + i) & 0x7FFFFFFFu;
        if(!duration || duration > RX_LINK_DURATION_MAX) {
            b->snapshot.rejected_batches = rx_link_add_saturated(b->snapshot.rejected_batches, 1);
            return false; // Validate the whole batch before changing statistics.
        }
    }
    const uint16_t missed = (uint16_t)(f->sequence - b->expected_sequence);
    if(missed >= 0x8000U) return false; // Duplicate/out-of-order; wrap at 65535 is valid.
    b->expected_sequence = (uint16_t)(f->sequence + 1);
    RxLinkSnapshot* s = &b->snapshot;
    s->lost_batches = rx_link_add_saturated(s->lost_batches, missed);
    const uint32_t drops = rx_link_read32(f->data + 4);
    if(drops > s->dropped_edges) s->dropped_edges = drops;
    for(unsigned i = 8; i < f->length && s->count < RX_LINK_SAMPLE_LIMIT; i += 4) {
        const uint32_t edge = rx_link_read32(f->data + i);
        const uint32_t duration = edge & 0x7FFFFFFFu;
        if(!s->count || duration < s->minimum) s->minimum = duration;
        if(duration > s->maximum) s->maximum = duration;
        b->duration_sum += duration;
        if(edge & 0x80000000u) b->high_sum += duration;
        unsigned bin = 0;
        for(uint32_t value = duration; value > 1 && bin < RX_LINK_BINS - 1; value >>= 1) bin++;
        s->bins[bin]++;
        s->count++;
    }
    return false;
}

bool rx_link_snapshot_decode(const RxLinkFrame* f, uint32_t session, RxLinkSnapshot* snapshot) {
    if(f->type != RxLinkStats || f->length != RX_LINK_STATS_SIZE ||
       rx_link_read32(f->data) != session)
        return false;
    RxLinkSnapshot s = {
        .count = rx_link_read32(f->data + 4),
        .minimum = rx_link_read32(f->data + 8),
        .maximum = rx_link_read32(f->data + 12),
        .mean = rx_link_read32(f->data + 16),
        .high_permille = rx_link_read32(f->data + 20),
        .dropped_edges = rx_link_read32(f->data + 24),
        .lost_batches = rx_link_read32(f->data + 28),
        .rejected_batches = rx_link_read32(f->data + 32),
    };
    if(s.count > RX_LINK_SAMPLE_LIMIT || s.maximum > RX_LINK_DURATION_MAX ||
       s.minimum > s.mean || s.mean > s.maximum || s.high_permille > 1000 ||
       (!s.count && (s.minimum || s.maximum || s.mean || s.high_permille)) ||
       (s.count && !s.minimum))
        return false;
    uint64_t total = 0;
    for(unsigned i = 0; i < RX_LINK_BINS; i++) {
        s.bins[i] = rx_link_read32(f->data + 36 + 4 * i);
        total += s.bins[i];
    }
    if(total != s.count) return false;
    *snapshot = s;
    return true;
}

void rx_link_client_begin(RxLinkClient* client, uint32_t session, uint32_t now_ms) {
    memset(client, 0, sizeof(*client));
    client->waiting = session != 0;
    client->session = session;
    client->last_seen_ms = now_ms;
}

void rx_link_client_stop(RxLinkClient* client) {
    client->waiting = client->connected = false;
    client->session = 0;
}

unsigned rx_link_client_accept(RxLinkClient* client, const RxLinkFrame* frame, uint32_t now_ms) {
    if(client->waiting && frame->type == RxLinkReady && frame->length == 9 &&
       !memcmp(frame->data, "RXA1", 4) && rx_link_read32(frame->data + 4) == client->session &&
       frame->data[8] == 1) {
        client->waiting = false;
        client->connected = true;
        client->last_seen_ms = now_ms;
        return RxLinkClientReady;
    }
    if(client->connected && rx_link_snapshot_decode(frame, client->session, &client->snapshot)) {
        client->last_seen_ms = now_ms;
        return RxLinkClientSnapshot;
    }
    return RxLinkClientIgnored;
}

bool rx_link_client_expired(const RxLinkClient* client, uint32_t now_ms) {
    return (client->waiting || client->connected) &&
           (uint32_t)(now_ms - client->last_seen_ms) >= (client->waiting ? 5000U : 2000U);
}
