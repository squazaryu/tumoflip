#include "tumokey_core.h"

#include <limits.h>
#include <string.h>

enum {
    TumoKeyInitialPayloadSize = TumoKeyHidReportSize - 7,
    TumoKeyContinuationPayloadSize = TumoKeyHidReportSize - 5,
    TumoKeyCborMaximumDepth = 4,
    TumoKeyCborMaximumItems = 32,
    TumoKeyCborMaximumContainerItems = 16,
    TumoKeyCtapGetInfo = 0x04,
    TumoKeyCtapSuccess = 0x00,
    TumoKeyCtapInvalidCommand = 0x01,
    TumoKeyCtapInvalidLength = 0x03,
    TumoKeyCtapInvalidCbor = 0x12,
};

static uint16_t tumokey_read_u16_be(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t tumokey_read_u32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           data[3];
}

static void tumokey_write_u16_be(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void tumokey_write_u32_be(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void tumokey_clear_transaction(TumoKeyCore* core) {
    core->active_cid = 0;
    core->deadline_ms = 0;
    core->expected_length = 0;
    core->received_length = 0;
    core->active_command = 0;
    core->next_sequence = 0;
    core->assembling = false;
    memset(core->request, 0, sizeof(core->request));
}

static bool tumokey_error(
    TumoKeyCore* core,
    uint32_t cid,
    TumoKeyHidError error,
    TumoKeyResponse* response) {
    if(response == NULL) return false;
    *response = (TumoKeyResponse){
        .cid = cid,
        .length = 1,
        .command = TumoKeyCommandError,
        .payload = {(uint8_t)error},
    };
    core->error_count++;
    return true;
}

static uint32_t tumokey_allocate_channel(TumoKeyCore* core) {
    core->next_channel++;
    if(core->next_channel == 0 || core->next_channel == UINT32_MAX) core->next_channel = 1;
    core->channel_cid = UINT32_C(0x544B0000) | (core->next_channel & UINT32_C(0xFFFF));
    if(core->channel_cid == UINT32_MAX) core->channel_cid--;
    return core->channel_cid;
}

static bool tumokey_cbor_read_argument(
    const uint8_t* data,
    size_t size,
    size_t* offset,
    uint8_t additional,
    uint32_t* value) {
    if(additional < 24) {
        *value = additional;
        return true;
    }
    size_t bytes = 0;
    if(additional == 24) {
        bytes = 1;
    } else if(additional == 25) {
        bytes = 2;
    } else if(additional == 26) {
        bytes = 4;
    } else {
        return false;
    }
    if(*offset > size || bytes > size - *offset) return false;
    uint32_t result = 0;
    for(size_t index = 0; index < bytes; index++) {
        result = (result << 8) | data[*offset + index];
    }
    *offset += bytes;
    *value = result;
    return true;
}

bool tumokey_cbor_validate(const uint8_t* data, size_t size) {
    if(data == NULL || size == 0 || size > TumoKeyMaximumMessageSize) return false;
    uint32_t remaining[TumoKeyCborMaximumDepth] = {1};
    size_t depth = 1;
    size_t offset = 0;
    uint32_t item_count = 0;

    while(depth > 0) {
        if(remaining[depth - 1] == 0) {
            depth--;
            continue;
        }
        if(offset >= size || ++item_count > TumoKeyCborMaximumItems) return false;
        remaining[depth - 1]--;

        const uint8_t initial = data[offset++];
        const uint8_t major = initial >> 5;
        const uint8_t additional = initial & 0x1F;
        uint32_t argument = 0;

        if(major == 7) {
            if(additional < 20 || additional > 22) return false;
            continue;
        }
        if(major == 6 || !tumokey_cbor_read_argument(data, size, &offset, additional, &argument)) {
            return false;
        }
        if(major <= 1) continue;
        if(major <= 3) {
            if(argument > TumoKeyMaximumMessageSize || offset > size || argument > size - offset) {
                return false;
            }
            offset += argument;
            continue;
        }
        if(major == 4 || major == 5) {
            if(argument > TumoKeyCborMaximumContainerItems) return false;
            if(major == 5 && argument > UINT32_MAX / 2U) return false;
            const uint32_t child_items = major == 5 ? argument * 2U : argument;
            if(child_items > 0) {
                if(depth >= TumoKeyCborMaximumDepth) return false;
                remaining[depth++] = child_items;
            }
            continue;
        }
        return false;
    }
    return offset == size;
}

static bool tumokey_get_info(TumoKeyResponse* response) {
    static const uint8_t get_info[] = {
        TumoKeyCtapSuccess,
        0xA4,
        0x01,
        0x81,
        0x68,
        'F',
        'I',
        'D',
        'O',
        '_',
        '2',
        '_',
        '0',
        0x03,
        0x50,
        'T',
        'u',
        'm',
        'o',
        'K',
        'e',
        'y',
        '-',
        'P',
        'h',
        'a',
        's',
        'e',
        '-',
        'A',
        0x00,
        0x05,
        0x19,
        0x02,
        0x00,
        0x09,
        0x81,
        0x63,
        'u',
        's',
        'b',
    };
    response->length = sizeof(get_info);
    memcpy(response->payload, get_info, sizeof(get_info));
    return true;
}

static bool tumokey_dispatch(TumoKeyCore* core, TumoKeyResponse* response) {
    response->cid = core->active_cid;
    response->command = core->active_command;
    core->last_command = core->active_command;
    core->request_count++;

    if(core->active_command == TumoKeyCommandPing) {
        response->length = core->expected_length;
        memcpy(response->payload, core->request, response->length);
        tumokey_clear_transaction(core);
        return true;
    }
    if(core->active_command == TumoKeyCommandCbor) {
        if(core->expected_length == 0) {
            response->length = 1;
            response->payload[0] = TumoKeyCtapInvalidLength;
        } else if(core->request[0] == TumoKeyCtapGetInfo) {
            if(core->expected_length != 1) {
                response->length = 1;
                response->payload[0] = TumoKeyCtapInvalidLength;
            } else {
                tumokey_get_info(response);
            }
        } else if(
            core->expected_length > 1 &&
            !tumokey_cbor_validate(core->request + 1, core->expected_length - 1)) {
            response->length = 1;
            response->payload[0] = TumoKeyCtapInvalidCbor;
        } else {
            response->length = 1;
            response->payload[0] = TumoKeyCtapInvalidCommand;
        }
        tumokey_clear_transaction(core);
        return true;
    }

    const uint32_t cid = core->active_cid;
    tumokey_clear_transaction(core);
    return tumokey_error(core, cid, TumoKeyHidErrorInvalidCommand, response);
}

static bool tumokey_handle_init(
    TumoKeyCore* core,
    uint32_t cid,
    const uint8_t* payload,
    uint16_t length,
    TumoKeyResponse* response) {
    if(length != 8) return tumokey_error(core, cid, TumoKeyHidErrorInvalidLength, response);
    if(cid != TumoKeyBroadcastCid && cid != core->channel_cid) {
        return tumokey_error(core, cid, TumoKeyHidErrorInvalidChannel, response);
    }
    tumokey_clear_transaction(core);
    const uint32_t assigned_cid = cid == TumoKeyBroadcastCid ? tumokey_allocate_channel(core) :
                                                               cid;
    *response = (TumoKeyResponse){
        .cid = cid,
        .length = 17,
        .command = TumoKeyCommandInit,
    };
    memcpy(response->payload, payload, 8);
    tumokey_write_u32_be(response->payload + 8, assigned_cid);
    response->payload[12] = 2;
    response->payload[13] = 0;
    response->payload[14] = 1;
    response->payload[15] = 0;
    response->payload[16] = 0x0C;
    core->last_command = TumoKeyCommandInit;
    core->request_count++;
    return true;
}

void tumokey_core_init(TumoKeyCore* core) {
    if(core == NULL) return;
    memset(core, 0, sizeof(*core));
}

void tumokey_core_disconnect(TumoKeyCore* core) {
    if(core == NULL) return;
    tumokey_clear_transaction(core);
    core->channel_cid = 0;
}

bool tumokey_core_feed(
    TumoKeyCore* core,
    const uint8_t* report,
    size_t report_size,
    uint32_t now_ms,
    TumoKeyResponse* response) {
    if(core == NULL || report == NULL || response == NULL || report_size < 4) return false;
    const uint32_t cid = tumokey_read_u32_be(report);
    if(report_size != TumoKeyHidReportSize) {
        return tumokey_error(core, cid, TumoKeyHidErrorInvalidLength, response);
    }

    const uint8_t marker = report[4];
    if((marker & 0x80U) != 0) {
        const uint8_t command = marker;
        const uint16_t length = tumokey_read_u16_be(report + 5);
        if(command == TumoKeyCommandInit) {
            if(length > TumoKeyInitialPayloadSize) {
                return tumokey_error(core, cid, TumoKeyHidErrorInvalidLength, response);
            }
            return tumokey_handle_init(core, cid, report + 7, length, response);
        }
        if(cid == TumoKeyBroadcastCid || cid == 0 || cid != core->channel_cid) {
            return tumokey_error(core, cid, TumoKeyHidErrorInvalidChannel, response);
        }
        if(command == TumoKeyCommandCancel) {
            if(length != 0) {
                return tumokey_error(core, cid, TumoKeyHidErrorInvalidLength, response);
            }
            if(core->assembling && cid == core->active_cid &&
               core->active_command == TumoKeyCommandCbor) {
                tumokey_clear_transaction(core);
            }
            return false;
        }
        if(core->assembling) {
            return tumokey_error(core, cid, TumoKeyHidErrorChannelBusy, response);
        }
        if(length > TumoKeyMaximumMessageSize) {
            return tumokey_error(core, cid, TumoKeyHidErrorInvalidLength, response);
        }

        core->active_cid = cid;
        core->active_command = command;
        core->expected_length = length;
        core->received_length = length < TumoKeyInitialPayloadSize ? length :
                                                                     TumoKeyInitialPayloadSize;
        core->next_sequence = 0;
        core->deadline_ms = now_ms + TumoKeyTransactionTimeoutMs;
        core->assembling = true;
        if(core->received_length > 0) {
            memcpy(core->request, report + 7, core->received_length);
        }
        if(core->received_length == core->expected_length) return tumokey_dispatch(core, response);
        return false;
    }

    if(cid == 0 || cid != core->channel_cid) {
        return tumokey_error(core, cid, TumoKeyHidErrorInvalidChannel, response);
    }
    if(!core->assembling || cid != core->active_cid) {
        return tumokey_error(core, cid, TumoKeyHidErrorInvalidSequence, response);
    }
    if(marker != core->next_sequence) {
        tumokey_clear_transaction(core);
        return tumokey_error(core, cid, TumoKeyHidErrorInvalidSequence, response);
    }
    const uint16_t remaining = core->expected_length - core->received_length;
    const uint16_t copied =
        remaining < TumoKeyContinuationPayloadSize ? remaining : TumoKeyContinuationPayloadSize;
    memcpy(core->request + core->received_length, report + 5, copied);
    core->received_length += copied;
    core->next_sequence++;
    core->deadline_ms = now_ms + TumoKeyTransactionTimeoutMs;
    if(core->received_length == core->expected_length) return tumokey_dispatch(core, response);
    return false;
}

bool tumokey_core_poll(TumoKeyCore* core, uint32_t now_ms, TumoKeyResponse* response) {
    if(core == NULL || response == NULL || !core->assembling ||
       (int32_t)(now_ms - core->deadline_ms) < 0) {
        return false;
    }
    const uint32_t cid = core->active_cid;
    tumokey_clear_transaction(core);
    return tumokey_error(core, cid, TumoKeyHidErrorMessageTimeout, response);
}

size_t tumokey_response_report_count(const TumoKeyResponse* response) {
    if(response == NULL || response->length > TumoKeyMaximumMessageSize) return 0;
    if(response->length <= TumoKeyInitialPayloadSize) return 1;
    const size_t remainder = response->length - TumoKeyInitialPayloadSize;
    return 1 + (remainder + TumoKeyContinuationPayloadSize - 1) / TumoKeyContinuationPayloadSize;
}

bool tumokey_response_encode_report(
    const TumoKeyResponse* response,
    size_t report_index,
    uint8_t report[TumoKeyHidReportSize]) {
    const size_t report_count = tumokey_response_report_count(response);
    if(report == NULL || report_index >= report_count) return false;
    memset(report, 0, TumoKeyHidReportSize);
    tumokey_write_u32_be(report, response->cid);
    if(report_index == 0) {
        report[4] = response->command;
        tumokey_write_u16_be(report + 5, response->length);
        const size_t copied = response->length < TumoKeyInitialPayloadSize ?
                                  response->length :
                                  TumoKeyInitialPayloadSize;
        memcpy(report + 7, response->payload, copied);
    } else {
        report[4] = (uint8_t)(report_index - 1);
        const size_t offset =
            TumoKeyInitialPayloadSize + (report_index - 1) * TumoKeyContinuationPayloadSize;
        const size_t remaining = response->length - offset;
        const size_t copied = remaining < TumoKeyContinuationPayloadSize ?
                                  remaining :
                                  TumoKeyContinuationPayloadSize;
        memcpy(report + 5, response->payload + offset, copied);
    }
    return true;
}
