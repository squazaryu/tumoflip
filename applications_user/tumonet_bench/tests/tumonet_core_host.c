#include "../core/tumonet_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_master(uint8_t master[TUMONET_MASTER_KEY_SIZE], uint8_t seed) {
    for(size_t i = 0; i < TUMONET_MASTER_KEY_SIZE; i++)
        master[i] = (uint8_t)(seed + i * 3U);
}

static void test_authenticated_round_trip(void) {
    uint8_t master[TUMONET_MASTER_KEY_SIZE];
    uint8_t payload[TUMONET_FRAGMENT_PAYLOAD_SIZE];
    uint8_t frame[TUMONET_MAX_FRAME_SIZE];
    uint8_t frame_size = 0U;
    uint32_t counter = 0U;
    TumoNetEndpoint sender;
    TumoNetEndpoint receiver;
    TumoNetPacket packet;

    fill_master(master, 0x11U);
    for(size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i ^ 0x5AU);
    assert(tumonet_endpoint_init(&sender, 1U, 2U, 0x10203040U, 7U, master));
    assert(tumonet_endpoint_init(&receiver, 2U, 1U, 0x10203040U, 100U, master));
    assert(
        tumonet_encode(
            &sender,
            TumoNetFlagData,
            9U,
            0U,
            1U,
            payload,
            sizeof(payload),
            frame,
            &frame_size,
            &counter) == TumoNetResultOk);
    assert(frame_size == TUMONET_MAX_FRAME_SIZE);
    assert(counter == 7U);
    assert(memcmp(&frame[TUMONET_HEADER_SIZE], payload, sizeof(payload)) != 0);
    assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultOk);
    assert(packet.counter == counter);
    assert(packet.payload_size == sizeof(payload));
    assert(memcmp(packet.payload, payload, sizeof(payload)) == 0);
    assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultDuplicate);

    tumonet_endpoint_clear(&sender);
    tumonet_endpoint_clear(&receiver);
    tumonet_crypto_zero(master, sizeof(master));
}

static void test_tamper_and_wrong_key(void) {
    uint8_t master[TUMONET_MASTER_KEY_SIZE];
    uint8_t wrong_master[TUMONET_MASTER_KEY_SIZE];
    const uint8_t payload[] = "authenticated";
    uint8_t frame[TUMONET_MAX_FRAME_SIZE];
    uint8_t frame_size = 0U;
    TumoNetEndpoint sender;
    TumoNetEndpoint receiver;
    TumoNetEndpoint wrong_receiver;
    TumoNetPacket packet;

    fill_master(master, 0x22U);
    memcpy(wrong_master, master, sizeof(master));
    wrong_master[4] ^= 0xFFU;
    assert(tumonet_endpoint_init(&sender, 1U, 2U, 0x55667788U, 1U, master));
    assert(tumonet_endpoint_init(&receiver, 2U, 1U, 0x55667788U, 1U, master));
    assert(tumonet_endpoint_init(&wrong_receiver, 2U, 1U, 0x55667788U, 1U, wrong_master));
    assert(
        tumonet_encode(
            &sender,
            TumoNetFlagData,
            4U,
            0U,
            1U,
            payload,
            sizeof(payload) - 1U,
            frame,
            &frame_size,
            NULL) == TumoNetResultOk);
    assert(
        tumonet_decode(&wrong_receiver, frame, frame_size, &packet) ==
        TumoNetResultAuthentication);
    frame[TUMONET_HEADER_SIZE + 1U] ^= 0x80U;
    assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultAuthentication);
    frame[TUMONET_HEADER_SIZE + 1U] ^= 0x80U;
    assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultOk);

    tumonet_endpoint_clear(&sender);
    tumonet_endpoint_clear(&receiver);
    tumonet_endpoint_clear(&wrong_receiver);
    tumonet_crypto_zero(master, sizeof(master));
    tumonet_crypto_zero(wrong_master, sizeof(wrong_master));
}

static void test_fragmentation_and_ack(void) {
    uint8_t master[TUMONET_MASTER_KEY_SIZE];
    uint8_t message[73];
    uint8_t assembled[sizeof(message)];
    TumoNetEndpoint sender;
    TumoNetEndpoint receiver;
    TumoNetReassembly reassembly;
    size_t assembled_size = 0U;

    fill_master(master, 0x33U);
    for(size_t i = 0; i < sizeof(message); i++)
        message[i] = (uint8_t)(0xE0U - i);
    assert(tumonet_endpoint_init(&sender, 1U, 2U, 0x90ABCDEFU, 40U, master));
    assert(tumonet_endpoint_init(&receiver, 2U, 1U, 0x90ABCDEFU, 80U, master));
    tumonet_reassembly_reset(&reassembly);

    for(uint8_t fragment = 0U; fragment < 4U; fragment++) {
        const size_t offset = fragment * TUMONET_FRAGMENT_PAYLOAD_SIZE;
        const size_t remaining = sizeof(message) - offset;
        const uint8_t size = remaining > TUMONET_FRAGMENT_PAYLOAD_SIZE ?
                                 TUMONET_FRAGMENT_PAYLOAD_SIZE :
                                 (uint8_t)remaining;
        uint8_t frame[TUMONET_MAX_FRAME_SIZE];
        uint8_t frame_size = 0U;
        uint32_t counter = 0U;
        TumoNetPacket packet;
        assert(
            tumonet_encode(
                &sender,
                TumoNetFlagData,
                0x1234U,
                fragment,
                4U,
                &message[offset],
                size,
                frame,
                &frame_size,
                &counter) == TumoNetResultOk);
        assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultOk);
        const TumoNetResult result = tumonet_reassembly_push(
            &reassembly, &packet, assembled, sizeof(assembled), &assembled_size);
        assert(result == (fragment == 3U ? TumoNetResultComplete : TumoNetResultPending));

        uint8_t ack_payload[TUMONET_ACK_PAYLOAD_SIZE];
        uint8_t ack_frame[TUMONET_MAX_FRAME_SIZE];
        uint8_t ack_frame_size = 0U;
        TumoNetPacket ack;
        tumonet_ack_encode_counter(counter, ack_payload);
        assert(
            tumonet_encode(
                &receiver,
                TumoNetFlagAck,
                0x1234U,
                0U,
                1U,
                ack_payload,
                sizeof(ack_payload),
                ack_frame,
                &ack_frame_size,
                NULL) == TumoNetResultOk);
        assert(tumonet_decode(&sender, ack_frame, ack_frame_size, &ack) == TumoNetResultOk);
        assert(ack.flags == TumoNetFlagAck);
        assert(tumonet_ack_decode_counter(ack.payload) == counter);
    }
    assert(assembled_size == sizeof(message));
    assert(memcmp(assembled, message, sizeof(message)) == 0);

    tumonet_endpoint_clear(&sender);
    tumonet_endpoint_clear(&receiver);
    tumonet_crypto_zero(master, sizeof(master));
}

static void test_replay_window(void) {
    uint8_t master[TUMONET_MASTER_KEY_SIZE];
    uint8_t oldest[TUMONET_MAX_FRAME_SIZE];
    uint8_t oldest_size = 0U;
    TumoNetEndpoint sender;
    TumoNetEndpoint receiver;
    TumoNetPacket packet;

    fill_master(master, 0x44U);
    assert(tumonet_endpoint_init(&sender, 1U, 2U, 0x11112222U, 1U, master));
    assert(tumonet_endpoint_init(&receiver, 2U, 1U, 0x11112222U, 1U, master));
    for(uint8_t i = 0U; i < 34U; i++) {
        uint8_t frame[TUMONET_MAX_FRAME_SIZE];
        uint8_t frame_size = 0U;
        assert(
            tumonet_encode(
                &sender, TumoNetFlagData, i + 1U, 0U, 1U, &i, 1U, frame, &frame_size, NULL) ==
            TumoNetResultOk);
        if(i == 0U) {
            memcpy(oldest, frame, frame_size);
            oldest_size = frame_size;
        }
        assert(tumonet_decode(&receiver, frame, frame_size, &packet) == TumoNetResultOk);
    }
    assert(tumonet_decode(&receiver, oldest, oldest_size, &packet) == TumoNetResultReplay);

    tumonet_endpoint_clear(&sender);
    tumonet_endpoint_clear(&receiver);
    tumonet_crypto_zero(master, sizeof(master));
}

int main(void) {
    test_authenticated_round_trip();
    test_tamper_and_wrong_key();
    test_fragmentation_and_ack();
    test_replay_window();
    puts("tumonet core: ok");
    return 0;
}
