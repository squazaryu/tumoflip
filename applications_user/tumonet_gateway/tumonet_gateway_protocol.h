#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMONET_GATEWAY_SCHEMA        1U
#define TUMONET_GATEWAY_TEXT_MAX      96U
#define TUMONET_GATEWAY_ENVELOPE_HEAD 13U
#define TUMONET_GATEWAY_ENVELOPE_MAX \
    (TUMONET_GATEWAY_ENVELOPE_HEAD + TUMONET_GATEWAY_TEXT_MAX)

typedef enum {
    TumoNetGatewayRouteInbox = 0U,
    TumoNetGatewayRouteRf = 1U,
} TumoNetGatewayRoute;

typedef struct {
    TumoNetGatewayRoute route;
    uint32_t source_id;
    uint32_t message_id;
    uint8_t text_size;
    char text[TUMONET_GATEWAY_TEXT_MAX + 1U];
} TumoNetGatewayEnvelope;

bool tumonet_gateway_envelope_encode(
    const TumoNetGatewayEnvelope* envelope,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size);

bool tumonet_gateway_envelope_decode(
    const uint8_t* input,
    size_t input_size,
    TumoNetGatewayEnvelope* envelope);

bool tumonet_gateway_text_valid(const uint8_t* text, size_t size);
const char* tumonet_gateway_route_name(TumoNetGatewayRoute route);
