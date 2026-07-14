#include "../tumonet_gateway_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_round_trip(void) {
    const char text[] = "hello from iphone";
    TumoNetGatewayEnvelope input = {
        .route = TumoNetGatewayRouteRf,
        .source_id = 0x10203040U,
        .message_id = 0x55667788U,
        .text_size = sizeof(text) - 1U,
    };
    memcpy(input.text, text, sizeof(text));

    uint8_t encoded[TUMONET_GATEWAY_ENVELOPE_MAX];
    size_t encoded_size = 0U;
    assert(tumonet_gateway_envelope_encode(
        &input, encoded, sizeof(encoded), &encoded_size));
    assert(encoded_size == TUMONET_GATEWAY_ENVELOPE_HEAD + input.text_size);

    TumoNetGatewayEnvelope output;
    assert(tumonet_gateway_envelope_decode(encoded, encoded_size, &output));
    assert(output.route == input.route);
    assert(output.source_id == input.source_id);
    assert(output.message_id == input.message_id);
    assert(output.text_size == input.text_size);
    assert(memcmp(output.text, input.text, input.text_size) == 0);
}

static void test_utf8_and_maximum(void) {
    const uint8_t utf8[] = {0xD0U, 0x9FU, 0xD1U, 0x80U, 0xD0U, 0xB8U, 0xD0U, 0xB2U, 0xD0U, 0xB5U, 0xD1U, 0x82U};
    assert(tumonet_gateway_text_valid(utf8, sizeof(utf8)));

    uint8_t maximum[TUMONET_GATEWAY_TEXT_MAX];
    memset(maximum, 'A', sizeof(maximum));
    assert(tumonet_gateway_text_valid(maximum, sizeof(maximum)));
    assert(!tumonet_gateway_text_valid(maximum, sizeof(maximum) + 1U));
}

static void test_rejects_invalid_input(void) {
    const uint8_t invalid_utf8[] = {0xE2U, 0x28U, 0xA1U};
    const uint8_t control[] = {'o', 'k', '\n'};
    assert(!tumonet_gateway_text_valid(invalid_utf8, sizeof(invalid_utf8)));
    assert(!tumonet_gateway_text_valid(control, sizeof(control)));

    TumoNetGatewayEnvelope envelope = {
        .route = TumoNetGatewayRouteInbox,
        .source_id = 1U,
        .message_id = 2U,
        .text_size = 1U,
        .text = "x",
    };
    uint8_t encoded[TUMONET_GATEWAY_ENVELOPE_MAX];
    size_t encoded_size = 0U;
    assert(tumonet_gateway_envelope_encode(
        &envelope, encoded, sizeof(encoded), &encoded_size));

    encoded[2] = 9U;
    assert(!tumonet_gateway_envelope_decode(encoded, encoded_size, &envelope));
    encoded[2] = TUMONET_GATEWAY_SCHEMA;
    encoded[4] = encoded[5] = encoded[6] = encoded[7] = 0U;
    assert(!tumonet_gateway_envelope_decode(encoded, encoded_size, &envelope));
}

int main(void) {
    test_round_trip();
    test_utf8_and_maximum();
    test_rejects_invalid_input();
    puts("tumonet gateway protocol: ok");
    return 0;
}
