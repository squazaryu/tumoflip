#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TumoKeyHidReportSize = 64,
    TumoKeyMaximumMessageSize = 512,
    TumoKeyTransactionTimeoutMs = 3000,
    TumoKeyCommandPing = 0x81,
    TumoKeyCommandInit = 0x86,
    TumoKeyCommandCbor = 0x90,
    TumoKeyCommandCancel = 0x91,
    TumoKeyCommandError = 0xBF,
};

#define TumoKeyBroadcastCid UINT32_MAX

typedef enum {
    TumoKeyHidErrorInvalidCommand = 0x01,
    TumoKeyHidErrorInvalidParameter = 0x02,
    TumoKeyHidErrorInvalidLength = 0x03,
    TumoKeyHidErrorInvalidSequence = 0x04,
    TumoKeyHidErrorMessageTimeout = 0x05,
    TumoKeyHidErrorChannelBusy = 0x06,
    TumoKeyHidErrorInvalidChannel = 0x0B,
    TumoKeyHidErrorOther = 0x7F,
} TumoKeyHidError;

typedef struct {
    uint32_t cid;
    uint16_t length;
    uint8_t command;
    uint8_t payload[TumoKeyMaximumMessageSize];
} TumoKeyResponse;

typedef struct {
    uint32_t channel_cid;
    uint32_t next_channel;
    uint32_t active_cid;
    uint32_t deadline_ms;
    uint16_t expected_length;
    uint16_t received_length;
    uint32_t request_count;
    uint32_t error_count;
    uint8_t active_command;
    uint8_t next_sequence;
    uint8_t last_command;
    bool assembling;
    uint8_t request[TumoKeyMaximumMessageSize];
} TumoKeyCore;

void tumokey_core_init(TumoKeyCore* core);
void tumokey_core_disconnect(TumoKeyCore* core);

bool tumokey_core_feed(
    TumoKeyCore* core,
    const uint8_t* report,
    size_t report_size,
    uint32_t now_ms,
    TumoKeyResponse* response);

bool tumokey_core_poll(TumoKeyCore* core, uint32_t now_ms, TumoKeyResponse* response);

bool tumokey_cbor_validate(const uint8_t* data, size_t size);

size_t tumokey_response_report_count(const TumoKeyResponse* response);
bool tumokey_response_encode_report(
    const TumoKeyResponse* response,
    size_t report_index,
    uint8_t report[TumoKeyHidReportSize]);
