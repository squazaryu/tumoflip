#pragma once

#include "tumonet_crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMONET_PROTOCOL_VERSION 1U
#define TUMONET_NODE_INTERNAL    1U
#define TUMONET_NODE_EXTERNAL    2U

#define TUMONET_HEADER_SIZE           19U
#define TUMONET_FRAGMENT_PAYLOAD_SIZE 24U
#define TUMONET_MAX_FRAGMENTS         6U
#define TUMONET_MAX_MESSAGE_SIZE      (TUMONET_FRAGMENT_PAYLOAD_SIZE * TUMONET_MAX_FRAGMENTS)
#define TUMONET_MAX_FRAME_SIZE \
    (TUMONET_HEADER_SIZE + TUMONET_FRAGMENT_PAYLOAD_SIZE + TUMONET_TAG_SIZE)
#define TUMONET_ACK_PAYLOAD_SIZE 4U

typedef enum {
    TumoNetFlagData = 0x01,
    TumoNetFlagAck = 0x02,
} TumoNetFlag;

typedef enum {
    TumoNetResultOk,
    TumoNetResultPending,
    TumoNetResultComplete,
    TumoNetResultDuplicate,
    TumoNetResultReplay,
    TumoNetResultAuthentication,
    TumoNetResultAddress,
    TumoNetResultSession,
    TumoNetResultFormat,
    TumoNetResultBounds,
    TumoNetResultCounter,
    TumoNetResultCrypto,
} TumoNetResult;

typedef struct {
    uint8_t flags;
    uint8_t source;
    uint8_t destination;
    uint32_t session_id;
    uint32_t counter;
    uint16_t message_id;
    uint8_t fragment_index;
    uint8_t fragment_count;
    uint8_t payload_size;
    uint8_t payload[TUMONET_FRAGMENT_PAYLOAD_SIZE];
} TumoNetPacket;

typedef struct {
    uint8_t local_id;
    uint8_t peer_id;
    uint32_t session_id;
    uint32_t next_tx_counter;
    uint32_t rx_highest_counter;
    uint32_t rx_window;
    bool rx_initialized;
    uint8_t tx_encryption_key[TUMONET_ENC_KEY_SIZE];
    uint8_t tx_authentication_key[TUMONET_MAC_KEY_SIZE];
    uint8_t rx_encryption_key[TUMONET_ENC_KEY_SIZE];
    uint8_t rx_authentication_key[TUMONET_MAC_KEY_SIZE];
} TumoNetEndpoint;

typedef struct {
    bool active;
    uint16_t message_id;
    uint8_t source;
    uint8_t fragment_count;
    uint8_t received_mask;
    uint8_t fragment_sizes[TUMONET_MAX_FRAGMENTS];
    uint8_t fragments[TUMONET_MAX_FRAGMENTS][TUMONET_FRAGMENT_PAYLOAD_SIZE];
} TumoNetReassembly;

bool tumonet_endpoint_init(
    TumoNetEndpoint* endpoint,
    uint8_t local_id,
    uint8_t peer_id,
    uint32_t session_id,
    uint32_t initial_tx_counter,
    const uint8_t master[TUMONET_MASTER_KEY_SIZE]);

void tumonet_endpoint_clear(TumoNetEndpoint* endpoint);

TumoNetResult tumonet_encode(
    TumoNetEndpoint* endpoint,
    uint8_t flags,
    uint16_t message_id,
    uint8_t fragment_index,
    uint8_t fragment_count,
    const uint8_t* payload,
    uint8_t payload_size,
    uint8_t frame[TUMONET_MAX_FRAME_SIZE],
    uint8_t* frame_size,
    uint32_t* encoded_counter);

TumoNetResult tumonet_decode(
    TumoNetEndpoint* endpoint,
    const uint8_t* frame,
    uint8_t frame_size,
    TumoNetPacket* packet);

void tumonet_reassembly_reset(TumoNetReassembly* reassembly);

TumoNetResult tumonet_reassembly_push(
    TumoNetReassembly* reassembly,
    const TumoNetPacket* packet,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_size);

void tumonet_ack_encode_counter(uint32_t counter, uint8_t output[TUMONET_ACK_PAYLOAD_SIZE]);
uint32_t tumonet_ack_decode_counter(const uint8_t input[TUMONET_ACK_PAYLOAD_SIZE]);
const char* tumonet_result_name(TumoNetResult result);
