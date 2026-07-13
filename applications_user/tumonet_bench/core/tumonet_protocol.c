#include "tumonet_protocol.h"

#include <limits.h>
#include <string.h>

#define TUMONET_MAGIC_0 0x54U
#define TUMONET_MAGIC_1 0x4EU

static void tumonet_write_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void tumonet_write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint16_t tumonet_read_u16(const uint8_t* input) {
    return ((uint16_t)input[0] << 8U) | input[1];
}

static uint32_t tumonet_read_u32(const uint8_t* input) {
    return ((uint32_t)input[0] << 24U) | ((uint32_t)input[1] << 16U) | ((uint32_t)input[2] << 8U) |
           input[3];
}

static bool tumonet_flags_valid(uint8_t flags) {
    return flags == TumoNetFlagData || flags == TumoNetFlagAck;
}

static void tumonet_build_nonce(
    uint32_t session_id,
    uint8_t source,
    uint8_t destination,
    uint32_t counter,
    uint16_t message_id,
    uint8_t fragment_index,
    uint8_t flags,
    uint8_t nonce[TUMONET_NONCE_SIZE]) {
    memset(nonce, 0, TUMONET_NONCE_SIZE);
    tumonet_write_u32(&nonce[0], session_id);
    nonce[4] = source;
    nonce[5] = destination;
    tumonet_write_u32(&nonce[6], counter);
    tumonet_write_u16(&nonce[10], message_id);
    nonce[12] = fragment_index;
    nonce[13] = flags;
}

static TumoNetResult tumonet_replay_accept(TumoNetEndpoint* endpoint, uint32_t counter) {
    if(!endpoint->rx_initialized) {
        endpoint->rx_initialized = true;
        endpoint->rx_highest_counter = counter;
        endpoint->rx_window = 1U;
        return TumoNetResultOk;
    }

    if(counter > endpoint->rx_highest_counter) {
        const uint32_t shift = counter - endpoint->rx_highest_counter;
        endpoint->rx_window = shift >= 32U ? 1U : (endpoint->rx_window << shift) | 1U;
        endpoint->rx_highest_counter = counter;
        return TumoNetResultOk;
    }

    const uint32_t distance = endpoint->rx_highest_counter - counter;
    if(distance >= 32U) return TumoNetResultReplay;
    const uint32_t bit = 1UL << distance;
    if((endpoint->rx_window & bit) != 0U) return TumoNetResultDuplicate;
    endpoint->rx_window |= bit;
    return TumoNetResultOk;
}

bool tumonet_endpoint_init(
    TumoNetEndpoint* endpoint,
    uint8_t local_id,
    uint8_t peer_id,
    uint32_t session_id,
    uint32_t initial_tx_counter,
    const uint8_t master[TUMONET_MASTER_KEY_SIZE]) {
    if(endpoint == NULL || master == NULL || local_id == 0U || peer_id == 0U ||
       local_id == peer_id || session_id == 0U || initial_tx_counter == 0U) {
        return false;
    }

    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->local_id = local_id;
    endpoint->peer_id = peer_id;
    endpoint->session_id = session_id;
    endpoint->next_tx_counter = initial_tx_counter;

    const bool tx_ok = tumonet_crypto_derive_direction(
        master, local_id, peer_id, endpoint->tx_encryption_key, endpoint->tx_authentication_key);
    const bool rx_ok = tumonet_crypto_derive_direction(
        master, peer_id, local_id, endpoint->rx_encryption_key, endpoint->rx_authentication_key);
    if(!tx_ok || !rx_ok) {
        tumonet_endpoint_clear(endpoint);
        return false;
    }
    return true;
}

void tumonet_endpoint_clear(TumoNetEndpoint* endpoint) {
    if(endpoint != NULL) tumonet_crypto_zero(endpoint, sizeof(*endpoint));
}

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
    uint32_t* encoded_counter) {
    if(endpoint == NULL || frame == NULL || frame_size == NULL || !tumonet_flags_valid(flags)) {
        return TumoNetResultFormat;
    }
    if(payload_size > TUMONET_FRAGMENT_PAYLOAD_SIZE || (payload_size > 0U && payload == NULL) ||
       fragment_count == 0U || fragment_count > TUMONET_MAX_FRAGMENTS ||
       fragment_index >= fragment_count) {
        return TumoNetResultBounds;
    }
    if(flags == TumoNetFlagAck && payload_size != TUMONET_ACK_PAYLOAD_SIZE) {
        return TumoNetResultFormat;
    }
    if(endpoint->next_tx_counter == 0U || endpoint->next_tx_counter == UINT32_MAX) {
        return TumoNetResultCounter;
    }

    const uint32_t counter = endpoint->next_tx_counter++;
    frame[0] = TUMONET_MAGIC_0;
    frame[1] = TUMONET_MAGIC_1;
    frame[2] = TUMONET_PROTOCOL_VERSION;
    frame[3] = flags;
    frame[4] = endpoint->local_id;
    frame[5] = endpoint->peer_id;
    tumonet_write_u32(&frame[6], endpoint->session_id);
    tumonet_write_u32(&frame[10], counter);
    tumonet_write_u16(&frame[14], message_id);
    frame[16] = fragment_index;
    frame[17] = fragment_count;
    frame[18] = payload_size;

    uint8_t nonce[TUMONET_NONCE_SIZE];
    tumonet_build_nonce(
        endpoint->session_id,
        endpoint->local_id,
        endpoint->peer_id,
        counter,
        message_id,
        fragment_index,
        flags,
        nonce);
    if(!tumonet_crypto_crypt(
           endpoint->tx_encryption_key, nonce, payload, &frame[TUMONET_HEADER_SIZE], payload_size)) {
        tumonet_crypto_zero(nonce, sizeof(nonce));
        return TumoNetResultCrypto;
    }
    tumonet_crypto_zero(nonce, sizeof(nonce));

    const size_t authenticated_size = TUMONET_HEADER_SIZE + payload_size;
    if(!tumonet_crypto_tag(
           endpoint->tx_authentication_key,
           frame,
           authenticated_size,
           &frame[authenticated_size])) {
        return TumoNetResultCrypto;
    }

    *frame_size = (uint8_t)(authenticated_size + TUMONET_TAG_SIZE);
    if(encoded_counter != NULL) *encoded_counter = counter;
    return TumoNetResultOk;
}

TumoNetResult tumonet_decode(
    TumoNetEndpoint* endpoint,
    const uint8_t* frame,
    uint8_t frame_size,
    TumoNetPacket* packet) {
    if(endpoint == NULL || frame == NULL || packet == NULL ||
       frame_size < TUMONET_HEADER_SIZE + TUMONET_TAG_SIZE) {
        return TumoNetResultFormat;
    }
    if(frame[0] != TUMONET_MAGIC_0 || frame[1] != TUMONET_MAGIC_1 ||
       frame[2] != TUMONET_PROTOCOL_VERSION || !tumonet_flags_valid(frame[3])) {
        return TumoNetResultFormat;
    }

    const uint8_t payload_size = frame[18];
    const size_t expected_size = TUMONET_HEADER_SIZE + payload_size + TUMONET_TAG_SIZE;
    if(payload_size > TUMONET_FRAGMENT_PAYLOAD_SIZE || expected_size != frame_size ||
       frame[17] == 0U || frame[17] > TUMONET_MAX_FRAGMENTS || frame[16] >= frame[17]) {
        return TumoNetResultBounds;
    }
    if(frame[3] == TumoNetFlagAck && payload_size != TUMONET_ACK_PAYLOAD_SIZE) {
        return TumoNetResultFormat;
    }
    if(frame[4] != endpoint->peer_id || frame[5] != endpoint->local_id) {
        return TumoNetResultAddress;
    }

    const uint32_t session_id = tumonet_read_u32(&frame[6]);
    if(session_id != endpoint->session_id) return TumoNetResultSession;

    const size_t authenticated_size = TUMONET_HEADER_SIZE + payload_size;
    uint8_t expected_tag[TUMONET_TAG_SIZE];
    if(!tumonet_crypto_tag(
           endpoint->rx_authentication_key, frame, authenticated_size, expected_tag)) {
        return TumoNetResultCrypto;
    }
    const bool authenticated = tumonet_crypto_tag_equal(expected_tag, &frame[authenticated_size]);
    tumonet_crypto_zero(expected_tag, sizeof(expected_tag));
    if(!authenticated) return TumoNetResultAuthentication;

    memset(packet, 0, sizeof(*packet));
    packet->flags = frame[3];
    packet->source = frame[4];
    packet->destination = frame[5];
    packet->session_id = session_id;
    packet->counter = tumonet_read_u32(&frame[10]);
    packet->message_id = tumonet_read_u16(&frame[14]);
    packet->fragment_index = frame[16];
    packet->fragment_count = frame[17];
    packet->payload_size = payload_size;

    uint8_t nonce[TUMONET_NONCE_SIZE];
    tumonet_build_nonce(
        packet->session_id,
        packet->source,
        packet->destination,
        packet->counter,
        packet->message_id,
        packet->fragment_index,
        packet->flags,
        nonce);
    if(!tumonet_crypto_crypt(
           endpoint->rx_encryption_key,
           nonce,
           &frame[TUMONET_HEADER_SIZE],
           packet->payload,
           payload_size)) {
        tumonet_crypto_zero(nonce, sizeof(nonce));
        tumonet_crypto_zero(packet, sizeof(*packet));
        return TumoNetResultCrypto;
    }
    tumonet_crypto_zero(nonce, sizeof(nonce));
    return tumonet_replay_accept(endpoint, packet->counter);
}

void tumonet_reassembly_reset(TumoNetReassembly* reassembly) {
    if(reassembly != NULL) memset(reassembly, 0, sizeof(*reassembly));
}

TumoNetResult tumonet_reassembly_push(
    TumoNetReassembly* reassembly,
    const TumoNetPacket* packet,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_size) {
    if(reassembly == NULL || packet == NULL || message == NULL || message_size == NULL ||
       packet->flags != TumoNetFlagData) {
        return TumoNetResultFormat;
    }
    if(packet->fragment_count == 0U || packet->fragment_count > TUMONET_MAX_FRAGMENTS ||
       packet->fragment_index >= packet->fragment_count ||
       packet->payload_size > TUMONET_FRAGMENT_PAYLOAD_SIZE) {
        return TumoNetResultBounds;
    }

    if(!reassembly->active || reassembly->message_id != packet->message_id ||
       reassembly->source != packet->source) {
        tumonet_reassembly_reset(reassembly);
        reassembly->active = true;
        reassembly->message_id = packet->message_id;
        reassembly->source = packet->source;
        reassembly->fragment_count = packet->fragment_count;
    }
    if(reassembly->fragment_count != packet->fragment_count) {
        tumonet_reassembly_reset(reassembly);
        return TumoNetResultFormat;
    }

    const uint8_t bit = (uint8_t)(1U << packet->fragment_index);
    if((reassembly->received_mask & bit) != 0U) return TumoNetResultDuplicate;
    memcpy(reassembly->fragments[packet->fragment_index], packet->payload, packet->payload_size);
    reassembly->fragment_sizes[packet->fragment_index] = packet->payload_size;
    reassembly->received_mask |= bit;

    const uint8_t complete_mask = (uint8_t)((1U << packet->fragment_count) - 1U);
    if(reassembly->received_mask != complete_mask) return TumoNetResultPending;

    size_t offset = 0U;
    for(uint8_t index = 0U; index < packet->fragment_count; index++) {
        const size_t fragment_size = reassembly->fragment_sizes[index];
        if(offset + fragment_size > message_capacity) {
            tumonet_reassembly_reset(reassembly);
            return TumoNetResultBounds;
        }
        memcpy(&message[offset], reassembly->fragments[index], fragment_size);
        offset += fragment_size;
    }
    *message_size = offset;
    tumonet_reassembly_reset(reassembly);
    return TumoNetResultComplete;
}

void tumonet_ack_encode_counter(uint32_t counter, uint8_t output[TUMONET_ACK_PAYLOAD_SIZE]) {
    tumonet_write_u32(output, counter);
}

uint32_t tumonet_ack_decode_counter(const uint8_t input[TUMONET_ACK_PAYLOAD_SIZE]) {
    return tumonet_read_u32(input);
}

const char* tumonet_result_name(TumoNetResult result) {
    switch(result) {
    case TumoNetResultOk:
        return "OK";
    case TumoNetResultPending:
        return "PENDING";
    case TumoNetResultComplete:
        return "COMPLETE";
    case TumoNetResultDuplicate:
        return "DUPLICATE";
    case TumoNetResultReplay:
        return "REPLAY";
    case TumoNetResultAuthentication:
        return "AUTH";
    case TumoNetResultAddress:
        return "ADDRESS";
    case TumoNetResultSession:
        return "SESSION";
    case TumoNetResultFormat:
        return "FORMAT";
    case TumoNetResultBounds:
        return "BOUNDS";
    case TumoNetResultCounter:
        return "COUNTER";
    case TumoNetResultCrypto:
        return "CRYPTO";
    default:
        return "UNKNOWN";
    }
}
