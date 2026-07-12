#include "../../applications_user/tumokey_phase_a/tumokey_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if(!(condition)) {                                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while(false)

static void write_u32_be(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint32_t read_u32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           data[3];
}

static bool contains(const uint8_t* data, size_t size, const char* value, size_t value_size) {
    if(value_size > size) return false;
    for(size_t offset = 0; offset <= size - value_size; offset++) {
        if(memcmp(data + offset, value, value_size) == 0) return true;
    }
    return false;
}

static void initial_report(
    uint8_t report[TumoKeyHidReportSize],
    uint32_t cid,
    uint8_t command,
    uint16_t length,
    const uint8_t* payload) {
    memset(report, 0, TumoKeyHidReportSize);
    write_u32_be(report, cid);
    report[4] = command;
    report[5] = (uint8_t)(length >> 8);
    report[6] = (uint8_t)length;
    const size_t copied = length < 57 ? length : 57;
    if(payload != NULL) memcpy(report + 7, payload, copied);
}

static bool allocate_channel(TumoKeyCore* core, uint32_t* channel) {
    uint8_t report[TumoKeyHidReportSize];
    const uint8_t nonce[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    initial_report(report, TumoKeyBroadcastCid, TumoKeyCommandInit, sizeof(nonce), nonce);
    TumoKeyResponse response;
    CHECK(tumokey_core_feed(core, report, sizeof(report), 0, &response));
    CHECK(response.cid == TumoKeyBroadcastCid);
    CHECK(response.command == TumoKeyCommandInit);
    CHECK(response.length == 17);
    CHECK(memcmp(response.payload, nonce, sizeof(nonce)) == 0);
    CHECK(response.payload[12] == 2);
    CHECK(response.payload[16] == 0x0C);
    *channel = read_u32_be(response.payload + 8);
    CHECK(*channel != 0 && *channel != TumoKeyBroadcastCid);
    return true;
}

static bool test_init_ping_and_get_info(void) {
    TumoKeyCore core;
    tumokey_core_init(&core);
    uint32_t channel = 0;
    CHECK(allocate_channel(&core, &channel));

    uint8_t report[TumoKeyHidReportSize];
    const uint8_t ping[] = {9, 8, 7, 6};
    initial_report(report, channel, TumoKeyCommandPing, sizeof(ping), ping);
    TumoKeyResponse response;
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 10, &response));
    CHECK(response.command == TumoKeyCommandPing);
    CHECK(response.length == sizeof(ping));
    CHECK(memcmp(response.payload, ping, sizeof(ping)) == 0);

    const uint8_t get_info = 0x04;
    initial_report(report, channel, TumoKeyCommandCbor, 1, &get_info);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 20, &response));
    CHECK(response.command == TumoKeyCommandCbor);
    CHECK(response.payload[0] == 0);
    CHECK(response.payload[1] == 0xA4);
    CHECK(response.length == 41);
    CHECK(contains(response.payload, response.length, "FIDO_2_0", 8));
    CHECK(contains(response.payload, response.length, "usb", 3));
    return true;
}

static bool test_fragmentation_cancel_and_timeout(void) {
    TumoKeyCore core;
    tumokey_core_init(&core);
    uint32_t channel = 0;
    CHECK(allocate_channel(&core, &channel));

    uint8_t payload[80];
    for(size_t index = 0; index < sizeof(payload); index++)
        payload[index] = (uint8_t)index;
    uint8_t report[TumoKeyHidReportSize];
    initial_report(report, channel, TumoKeyCommandPing, sizeof(payload), payload);
    TumoKeyResponse response;
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 100, &response));
    memset(report, 0, sizeof(report));
    write_u32_be(report, channel);
    report[4] = 0;
    memcpy(report + 5, payload + 57, sizeof(payload) - 57);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 101, &response));
    CHECK(response.length == sizeof(payload));
    CHECK(memcmp(response.payload, payload, sizeof(payload)) == 0);
    CHECK(tumokey_response_report_count(&response) == 2);
    CHECK(tumokey_response_encode_report(&response, 1, report));
    CHECK(report[4] == 0);

    uint8_t cbor[80] = {0x01};
    initial_report(report, channel, TumoKeyCommandCbor, sizeof(cbor), cbor);
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 200, &response));
    initial_report(report, channel, TumoKeyCommandPing, 0, NULL);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 200, &response));
    CHECK(response.payload[0] == TumoKeyHidErrorChannelBusy);

    initial_report(report, channel, TumoKeyCommandCancel, 1, cbor);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 201, &response));
    CHECK(response.payload[0] == TumoKeyHidErrorInvalidLength);
    CHECK(core.assembling);

    initial_report(report, channel, TumoKeyCommandCancel, 0, NULL);
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 202, &response));
    CHECK(!core.assembling);

    initial_report(report, channel, TumoKeyCommandPing, sizeof(payload), payload);
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 300, &response));
    CHECK(!tumokey_core_poll(&core, 3299, &response));
    CHECK(tumokey_core_poll(&core, 3300, &response));
    CHECK(response.command == TumoKeyCommandError);
    CHECK(response.payload[0] == TumoKeyHidErrorMessageTimeout);
    return true;
}

static bool test_sequence_and_resynchronization(void) {
    TumoKeyCore core;
    tumokey_core_init(&core);
    uint32_t channel = 0;
    CHECK(allocate_channel(&core, &channel));

    uint8_t payload[80] = {0};
    uint8_t report[TumoKeyHidReportSize];
    TumoKeyResponse response;
    initial_report(report, channel, TumoKeyCommandPing, sizeof(payload), payload);
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 10, &response));
    memset(report, 0, sizeof(report));
    write_u32_be(report, channel);
    report[4] = 1;
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 11, &response));
    CHECK(response.payload[0] == TumoKeyHidErrorInvalidSequence);
    CHECK(!core.assembling);

    initial_report(report, channel, TumoKeyCommandPing, sizeof(payload), payload);
    CHECK(!tumokey_core_feed(&core, report, sizeof(report), 20, &response));
    const uint8_t nonce[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    initial_report(report, channel, TumoKeyCommandInit, sizeof(nonce), nonce);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 21, &response));
    CHECK(response.command == TumoKeyCommandInit);
    CHECK(!core.assembling);
    CHECK(read_u32_be(response.payload + 8) == channel);
    return true;
}

static bool test_malformed_input_fails_closed(void) {
    TumoKeyCore core;
    tumokey_core_init(&core);
    uint32_t channel = 0;
    CHECK(allocate_channel(&core, &channel));
    uint8_t report[TumoKeyHidReportSize];
    TumoKeyResponse response;

    initial_report(report, channel, TumoKeyCommandPing, 513, NULL);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 0, &response));
    CHECK(response.payload[0] == TumoKeyHidErrorInvalidLength);

    initial_report(report, channel + 1, TumoKeyCommandPing, 0, NULL);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 0, &response));
    CHECK(response.payload[0] == TumoKeyHidErrorInvalidChannel);

    const uint8_t malformed_cbor[] = {0x01, 0xBF, 0x01, 0x02, 0xFF};
    initial_report(report, channel, TumoKeyCommandCbor, sizeof(malformed_cbor), malformed_cbor);
    CHECK(tumokey_core_feed(&core, report, sizeof(report), 0, &response));
    CHECK(response.command == TumoKeyCommandCbor);
    CHECK(response.payload[0] == 0x12);

    CHECK(!tumokey_cbor_validate(NULL, 0));
    const uint8_t valid_map[] = {0xA1, 0x01, 0x82, 0xF4, 0xF5};
    CHECK(tumokey_cbor_validate(valid_map, sizeof(valid_map)));
    const uint8_t too_deep[] = {0x81, 0x81, 0x81, 0x81, 0x00};
    CHECK(!tumokey_cbor_validate(too_deep, sizeof(too_deep)));
    return true;
}

static bool test_deterministic_malformed_corpus(void) {
    TumoKeyCore core;
    tumokey_core_init(&core);
    TumoKeyResponse response;
    uint8_t report[TumoKeyHidReportSize];
    uint32_t state = UINT32_C(0x64A0BEEF);

    for(size_t iteration = 0; iteration < 5000; iteration++) {
        for(size_t index = 0; index < sizeof(report); index++) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            report[index] = (uint8_t)(state >> 24);
        }
        const size_t report_size = iteration % (TumoKeyHidReportSize + 1U);
        tumokey_core_feed(&core, report, report_size, (uint32_t)iteration, &response);
        tumokey_core_poll(&core, (uint32_t)iteration + TumoKeyTransactionTimeoutMs, &response);
        if((iteration % 97U) == 0) tumokey_core_disconnect(&core);
    }
    return true;
}

int main(void) {
    if(!test_init_ping_and_get_info() || !test_fragmentation_cancel_and_timeout() ||
       !test_sequence_and_resynchronization() || !test_malformed_input_fails_closed() ||
       !test_deterministic_malformed_corpus()) {
        return 1;
    }
    puts("tumokey_phase_a_host_test: PASS");
    return 0;
}
