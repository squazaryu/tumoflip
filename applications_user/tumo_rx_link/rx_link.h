#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Private, versioned UART contract. No RF transmit or credential commands. */
#define RX_LINK_PAYLOAD_MAX  128U
#define RX_LINK_FRAME_MAX    (RX_LINK_PAYLOAD_MAX + 9U)
#define RX_LINK_BINS         16U
#define RX_LINK_SAMPLE_LIMIT 1000000U
#define RX_LINK_DURATION_MAX 1000000U
#define RX_LINK_STATS_SIZE   100U
#define RX_LINK_BAUD         230400U

enum {
    RxLinkHello = 1,
    RxLinkReady,
    RxLinkEdges,
    RxLinkQuery,
    RxLinkStats,
    RxLinkStop,
};

typedef struct {
    uint8_t type;
    uint8_t length;
    uint16_t sequence;
    uint8_t data[RX_LINK_PAYLOAD_MAX];
} RxLinkFrame;

typedef struct {
    uint8_t buffer[RX_LINK_FRAME_MAX];
    uint16_t used;
    uint16_t expected;
    uint32_t errors;
} RxLinkParser;

typedef struct {
    uint32_t count;
    uint32_t minimum;
    uint32_t maximum;
    uint32_t mean;
    uint32_t high_permille;
    uint32_t dropped_edges;
    uint32_t lost_batches;
    uint32_t rejected_batches;
    uint32_t bins[RX_LINK_BINS];
} RxLinkSnapshot;

typedef struct {
    bool active;
    uint32_t session;
    uint16_t expected_sequence;
    uint64_t duration_sum;
    uint64_t high_sum;
    RxLinkSnapshot snapshot;
} RxLinkBackend;

uint32_t rx_link_read32(const uint8_t* bytes);
void rx_link_write32(uint8_t* bytes, uint32_t value);
size_t rx_link_encode(const RxLinkFrame* frame, uint8_t* output, size_t capacity);
bool rx_link_feed(RxLinkParser* parser, uint8_t byte, RxLinkFrame* frame);
bool rx_link_backend_handle(RxLinkBackend* backend, const RxLinkFrame* frame, RxLinkFrame* reply);
enum {RxLinkBackendIgnored, RxLinkBackendActivity, RxLinkBackendReply};
unsigned rx_link_backend_feed(RxLinkBackend* backend, RxLinkParser* parser, uint8_t byte, RxLinkFrame* reply);
bool rx_link_snapshot_decode(const RxLinkFrame* frame, uint32_t session, RxLinkSnapshot* snapshot);

typedef struct {
    bool waiting;
    bool connected;
    uint32_t session;
    uint32_t last_seen_ms;
    RxLinkSnapshot snapshot;
} RxLinkClient;

enum {
    RxLinkClientIgnored,
    RxLinkClientReady,
    RxLinkClientSnapshot
};
void rx_link_client_begin(RxLinkClient* client, uint32_t session, uint32_t now_ms);
void rx_link_client_stop(RxLinkClient* client);
unsigned rx_link_client_accept(RxLinkClient* client, const RxLinkFrame* frame, uint32_t now_ms);
bool rx_link_client_expired(const RxLinkClient* client, uint32_t now_ms);
