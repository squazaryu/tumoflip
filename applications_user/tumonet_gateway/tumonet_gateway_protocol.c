#include "tumonet_gateway_protocol.h"

#include <string.h>

#define TUMONET_GATEWAY_MAGIC_0 'T'
#define TUMONET_GATEWAY_MAGIC_1 'N'

static void tumonet_gateway_write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static uint32_t tumonet_gateway_read_u32(const uint8_t* input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) |
           ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

bool tumonet_gateway_text_valid(const uint8_t* text, size_t size) {
    if(text == NULL || size == 0U || size > TUMONET_GATEWAY_TEXT_MAX) return false;

    size_t index = 0U;
    while(index < size) {
        const uint8_t first = text[index];
        if(first >= 0x20U && first <= 0x7EU) {
            index++;
            continue;
        }

        size_t continuation_count;
        uint32_t codepoint;
        if(first >= 0xC2U && first <= 0xDFU) {
            continuation_count = 1U;
            codepoint = first & 0x1FU;
        } else if(first >= 0xE0U && first <= 0xEFU) {
            continuation_count = 2U;
            codepoint = first & 0x0FU;
        } else if(first >= 0xF0U && first <= 0xF4U) {
            continuation_count = 3U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }

        if(index + continuation_count >= size) return false;
        for(size_t offset = 1U; offset <= continuation_count; offset++) {
            const uint8_t next = text[index + offset];
            if((next & 0xC0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }

        if((continuation_count == 2U && codepoint < 0x800U) ||
           (continuation_count == 3U && codepoint < 0x10000U) ||
           (codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

bool tumonet_gateway_envelope_encode(
    const TumoNetGatewayEnvelope* envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size) {
    if(envelope == NULL || output == NULL || output_size == NULL ||
       envelope->source_id == 0U || envelope->message_id == 0U ||
       (envelope->route != TumoNetGatewayRouteInbox &&
        envelope->route != TumoNetGatewayRouteRf) ||
       !tumonet_gateway_text_valid((const uint8_t*)envelope->text, envelope->text_size)) {
        return false;
    }

    const size_t encoded_size = TUMONET_GATEWAY_ENVELOPE_HEAD + envelope->text_size;
    if(output_capacity < encoded_size) return false;

    output[0] = TUMONET_GATEWAY_MAGIC_0;
    output[1] = TUMONET_GATEWAY_MAGIC_1;
    output[2] = TUMONET_GATEWAY_SCHEMA;
    output[3] = (uint8_t)envelope->route;
    tumonet_gateway_write_u32(&output[4], envelope->source_id);
    tumonet_gateway_write_u32(&output[8], envelope->message_id);
    output[12] = envelope->text_size;
    memcpy(&output[TUMONET_GATEWAY_ENVELOPE_HEAD], envelope->text, envelope->text_size);
    *output_size = encoded_size;
    return true;
}

bool tumonet_gateway_envelope_decode(
    const uint8_t* input,
    size_t input_size,
    TumoNetGatewayEnvelope* envelope) {
    if(input == NULL || envelope == NULL || input_size < TUMONET_GATEWAY_ENVELOPE_HEAD ||
       input[0] != TUMONET_GATEWAY_MAGIC_0 || input[1] != TUMONET_GATEWAY_MAGIC_1 ||
       input[2] != TUMONET_GATEWAY_SCHEMA || input[3] > TumoNetGatewayRouteRf) {
        return false;
    }

    const uint8_t text_size = input[12];
    if(text_size == 0U || text_size > TUMONET_GATEWAY_TEXT_MAX ||
       input_size != TUMONET_GATEWAY_ENVELOPE_HEAD + text_size ||
       !tumonet_gateway_text_valid(&input[TUMONET_GATEWAY_ENVELOPE_HEAD], text_size)) {
        return false;
    }

    memset(envelope, 0, sizeof(*envelope));
    envelope->route = (TumoNetGatewayRoute)input[3];
    envelope->source_id = tumonet_gateway_read_u32(&input[4]);
    envelope->message_id = tumonet_gateway_read_u32(&input[8]);
    if(envelope->source_id == 0U || envelope->message_id == 0U) return false;
    envelope->text_size = text_size;
    memcpy(envelope->text, &input[TUMONET_GATEWAY_ENVELOPE_HEAD], text_size);
    envelope->text[text_size] = '\0';
    return true;
}

const char* tumonet_gateway_route_name(TumoNetGatewayRoute route) {
    return route == TumoNetGatewayRouteRf ? "RF" : "Inbox";
}
