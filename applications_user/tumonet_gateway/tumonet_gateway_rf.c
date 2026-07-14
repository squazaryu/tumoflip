#include "tumonet_gateway_rf.h"

#include "../tumonet_bench/core/tumonet_crypto.h"
#include "../tumonet_bench/core/tumonet_protocol.h"
#include "../tumonet_bench/radio/tumonet_radio.h"

#include <furi.h>
#include <furi_hal.h>

#include <string.h>

#define TUMONET_GATEWAY_RF_RETRIES 2U

static uint32_t tumonet_gateway_random_nonzero(void) {
    uint32_t value = furi_hal_random_get();
    return value == 0U ? 1U : value;
}

static TumoNetRadioDirection
    tumonet_gateway_reverse_direction(TumoNetRadioDirection direction) {
    return direction == TumoNetRadioDirectionInternalToExternal ?
               TumoNetRadioDirectionExternalToInternal :
               TumoNetRadioDirectionInternalToExternal;
}

static bool tumonet_gateway_transfer(
    TumoNetRadio* radio,
    TumoNetRadioDirection direction,
    const uint8_t* frame,
    uint8_t frame_size,
    uint8_t* received,
    uint8_t* received_size,
    volatile bool* cancel_requested,
    uint8_t* retries,
    TumoNetRadioResult* final_result) {
    for(uint8_t attempt = 0U; attempt <= TUMONET_GATEWAY_RF_RETRIES; attempt++) {
        const TumoNetRadioResult radio_result = tumonet_radio_transfer(
            radio, direction, frame, frame_size, received, received_size, cancel_requested);
        *final_result = radio_result;
        if(radio_result == TumoNetRadioResultOk) return true;
        if(!tumonet_radio_result_is_retryable(radio_result) ||
           (cancel_requested != NULL && *cancel_requested)) {
            return false;
        }
        if(attempt < TUMONET_GATEWAY_RF_RETRIES) (*retries)++;
    }
    return false;
}

static bool tumonet_gateway_ack(
    TumoNetRadio* radio,
    TumoNetEndpoint* receiver,
    TumoNetEndpoint* sender,
    TumoNetRadioDirection direction,
    uint16_t message_id,
    uint32_t acknowledged_counter,
    volatile bool* cancel_requested,
    uint8_t* retries,
    char* status,
    size_t status_size) {
    uint8_t payload[TUMONET_ACK_PAYLOAD_SIZE];
    uint8_t frame[TUMONET_MAX_FRAME_SIZE];
    uint8_t frame_size = 0U;
    uint8_t received[TUMONET_MAX_FRAME_SIZE];
    uint8_t received_size = 0U;
    TumoNetPacket packet;

    tumonet_ack_encode_counter(acknowledged_counter, payload);
    const TumoNetResult encoded = tumonet_encode(
        receiver,
        TumoNetFlagAck,
        message_id,
        0U,
        1U,
        payload,
        sizeof(payload),
        frame,
        &frame_size,
        NULL);
    if(encoded != TumoNetResultOk) {
        strlcpy(status, tumonet_result_name(encoded), status_size);
        return false;
    }

    TumoNetRadioResult radio_result = TumoNetRadioResultInit;
    if(!tumonet_gateway_transfer(
           radio,
           tumonet_gateway_reverse_direction(direction),
           frame,
           frame_size,
           received,
           &received_size,
           cancel_requested,
           retries,
           &radio_result)) {
        strlcpy(status, tumonet_radio_result_name(radio_result), status_size);
        return false;
    }

    const TumoNetResult decoded = tumonet_decode(sender, received, received_size, &packet);
    if(decoded != TumoNetResultOk || packet.flags != TumoNetFlagAck ||
       packet.payload_size != TUMONET_ACK_PAYLOAD_SIZE ||
       tumonet_ack_decode_counter(packet.payload) != acknowledged_counter) {
        strlcpy(
            status,
            decoded == TumoNetResultOk ? "BAD ACK" : tumonet_result_name(decoded),
            status_size);
        return false;
    }
    return true;
}

bool tumonet_gateway_rf_deliver(
    const uint8_t* message,
    size_t message_size,
    volatile bool* cancel_requested,
    TumoNetGatewayRfResult* result) {
    if(message == NULL || message_size == 0U || message_size > TUMONET_MAX_MESSAGE_SIZE ||
       result == NULL) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->radio_result = TumoNetRadioResultInit;
    strlcpy(result->status, "RF init", sizeof(result->status));

    bool delivered = false;
    uint8_t master_key[TUMONET_MASTER_KEY_SIZE];
    uint8_t assembled[TUMONET_MAX_MESSAGE_SIZE];
    size_t assembled_size = 0U;
    TumoNetEndpoint internal_endpoint = {0};
    TumoNetEndpoint external_endpoint = {0};
    TumoNetReassembly reassembly;
    TumoNetRadio* radio = tumonet_radio_alloc();

    if(radio == NULL) {
        strlcpy(result->status, "NO MEMORY", sizeof(result->status));
        goto cleanup;
    }

    TumoNetRadioResult radio_result =
        tumonet_radio_open_with_owner(radio, "tumonet_gateway");
    result->radio_result = radio_result;
    if(radio_result != TumoNetRadioResultOk) {
        strlcpy(result->status, tumonet_radio_result_name(radio_result), sizeof(result->status));
        goto cleanup;
    }
    result->frequency = tumonet_radio_frequency(radio);

    furi_hal_random_fill_buf(master_key, sizeof(master_key));
    const uint32_t session_id = tumonet_gateway_random_nonzero();
    if(!tumonet_endpoint_init(
           &internal_endpoint,
           TUMONET_NODE_INTERNAL,
           TUMONET_NODE_EXTERNAL,
           session_id,
           tumonet_gateway_random_nonzero(),
           master_key) ||
       !tumonet_endpoint_init(
           &external_endpoint,
           TUMONET_NODE_EXTERNAL,
           TUMONET_NODE_INTERNAL,
           session_id,
           tumonet_gateway_random_nonzero(),
           master_key)) {
        strlcpy(result->status, "KEY DERIVATION", sizeof(result->status));
        goto cleanup;
    }

    tumonet_reassembly_reset(&reassembly);
    uint16_t protocol_message_id = (uint16_t)tumonet_gateway_random_nonzero();
    if(protocol_message_id == 0U) protocol_message_id = 1U;
    const uint8_t fragment_count =
        (uint8_t)((message_size + TUMONET_FRAGMENT_PAYLOAD_SIZE - 1U) /
                  TUMONET_FRAGMENT_PAYLOAD_SIZE);

    for(uint8_t fragment = 0U; fragment < fragment_count; fragment++) {
        if(cancel_requested != NULL && *cancel_requested) {
            strlcpy(result->status, "CANCELLED", sizeof(result->status));
            goto cleanup;
        }

        const size_t offset = (size_t)fragment * TUMONET_FRAGMENT_PAYLOAD_SIZE;
        const size_t remaining = message_size - offset;
        const uint8_t fragment_size =
            (uint8_t)(remaining > TUMONET_FRAGMENT_PAYLOAD_SIZE ?
                          TUMONET_FRAGMENT_PAYLOAD_SIZE :
                          remaining);
        uint8_t frame[TUMONET_MAX_FRAME_SIZE];
        uint8_t frame_size = 0U;
        uint32_t frame_counter = 0U;
        TumoNetResult protocol_result = tumonet_encode(
            &internal_endpoint,
            TumoNetFlagData,
            protocol_message_id,
            fragment,
            fragment_count,
            &message[offset],
            fragment_size,
            frame,
            &frame_size,
            &frame_counter);
        if(protocol_result != TumoNetResultOk) {
            strlcpy(result->status, tumonet_result_name(protocol_result), sizeof(result->status));
            goto cleanup;
        }

        uint8_t received[TUMONET_MAX_FRAME_SIZE];
        uint8_t received_size = 0U;
        if(!tumonet_gateway_transfer(
               radio,
               TumoNetRadioDirectionInternalToExternal,
               frame,
               frame_size,
               received,
               &received_size,
               cancel_requested,
               &result->retries,
               &radio_result)) {
            result->radio_result = radio_result;
            strlcpy(
                result->status, tumonet_radio_result_name(radio_result), sizeof(result->status));
            goto cleanup;
        }

        TumoNetPacket packet;
        protocol_result = tumonet_decode(
            &external_endpoint, received, received_size, &packet);
        if(protocol_result != TumoNetResultOk) {
            strlcpy(result->status, tumonet_result_name(protocol_result), sizeof(result->status));
            goto cleanup;
        }
        protocol_result = tumonet_reassembly_push(
            &reassembly, &packet, assembled, sizeof(assembled), &assembled_size);
        const TumoNetResult expected = fragment + 1U == fragment_count ?
                                          TumoNetResultComplete :
                                          TumoNetResultPending;
        if(protocol_result != expected) {
            strlcpy(result->status, tumonet_result_name(protocol_result), sizeof(result->status));
            goto cleanup;
        }
        result->fragments++;

        if(!tumonet_gateway_ack(
               radio,
               &external_endpoint,
               &internal_endpoint,
               TumoNetRadioDirectionInternalToExternal,
               protocol_message_id,
               frame_counter,
               cancel_requested,
               &result->retries,
               result->status,
               sizeof(result->status))) {
            goto cleanup;
        }
    }

    if(assembled_size != message_size || memcmp(assembled, message, message_size) != 0) {
        strlcpy(result->status, "PAYLOAD MISMATCH", sizeof(result->status));
        goto cleanup;
    }

    strlcpy(result->status, "Delivered", sizeof(result->status));
    result->radio_result = TumoNetRadioResultOk;
    delivered = true;

cleanup:
    tumonet_endpoint_clear(&internal_endpoint);
    tumonet_endpoint_clear(&external_endpoint);
    tumonet_crypto_zero(master_key, sizeof(master_key));
    tumonet_crypto_zero(assembled, sizeof(assembled));
    if(radio != NULL) tumonet_radio_free(radio);
    return delivered;
}
