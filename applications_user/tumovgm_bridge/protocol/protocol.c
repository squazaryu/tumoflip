#include "protocol.h"

#include <limits.h>
#include <string.h>

enum {
    TumovgmOffsetMajor = 4,
    TumovgmOffsetMinor = 5,
    TumovgmOffsetKind = 6,
    TumovgmOffsetFlags = 7,
    TumovgmOffsetSequence = 8,
    TumovgmOffsetMessage = 10,
    TumovgmOffsetPayloadLength = 12,
};

static const uint8_t tumovgm_magic[] = {
    TUMOVGM_PROTOCOL_MAGIC_0,
    TUMOVGM_PROTOCOL_MAGIC_1,
    TUMOVGM_PROTOCOL_MAGIC_2,
    TUMOVGM_PROTOCOL_MAGIC_3,
};

static uint16_t tumovgm_read_u16_le(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void tumovgm_write_u16_le(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value & UINT16_C(0x00FF));
    data[1] = (uint8_t)(value >> 8);
}

static uint8_t tumovgm_min_u8(uint8_t first, uint8_t second) {
    return first < second ? first : second;
}

static bool tumovgm_magic_prefix_matches(const uint8_t* data, size_t length) {
    const size_t compare_length = length < sizeof(tumovgm_magic) ? length : sizeof(tumovgm_magic);
    return memcmp(data, tumovgm_magic, compare_length) == 0;
}

static size_t tumovgm_resync_skip(const uint8_t* input, size_t input_length) {
    for(size_t offset = 1; offset < input_length; offset++) {
        if(tumovgm_magic_prefix_matches(input + offset, input_length - offset)) {
            return offset;
        }
    }
    return input_length;
}

static bool tumovgm_frame_semantics_valid(const TumovgmFrame* frame) {
    if(frame == NULL || frame->major == 0 || frame->message == 0) {
        return false;
    }
    if(frame->kind < TumovgmFrameKindRequest || frame->kind > TumovgmFrameKindError) {
        return false;
    }
    if((frame->flags & (uint8_t)~TumovgmFrameKnownFlags) != 0) {
        return false;
    }
    if(frame->payload_length > TUMOVGM_PROTOCOL_MAX_PAYLOAD) {
        return false;
    }
    if(frame->payload_length > 0 && frame->payload == NULL) {
        return false;
    }
    if(frame->kind == TumovgmFrameKindEvent) {
        return frame->sequence == 0;
    }
    return frame->sequence != 0;
}

uint16_t tumovgm_crc16_ccitt_false(const uint8_t* data, size_t length) {
    uint16_t crc = UINT16_C(0xFFFF);
    if(data == NULL && length != 0) {
        return 0;
    }

    for(size_t index = 0; index < length; index++) {
        crc ^= (uint16_t)data[index] << 8;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if((crc & UINT16_C(0x8000)) != 0) {
                crc = (uint16_t)((crc << 1) ^ UINT16_C(0x1021));
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

size_t tumovgm_frame_encoded_size(const TumovgmFrame* frame) {
    if(frame == NULL || frame->payload_length > TUMOVGM_PROTOCOL_MAX_PAYLOAD) {
        return 0;
    }
    return TumovgmFrameHeaderSize + (size_t)frame->payload_length + TumovgmFrameCrcSize;
}

TumovgmCodecStatus tumovgm_frame_encode(
    const TumovgmFrame* frame,
    uint8_t* output,
    size_t output_capacity,
    size_t* written) {
    if(written != NULL) {
        *written = 0;
    }
    if(frame == NULL || output == NULL || written == NULL) {
        return TumovgmCodecStatusInvalidArgument;
    }
    if(frame->payload_length > TUMOVGM_PROTOCOL_MAX_PAYLOAD) {
        return TumovgmCodecStatusPayloadTooLarge;
    }
    if(!tumovgm_frame_semantics_valid(frame)) {
        return TumovgmCodecStatusBadSemantics;
    }

    const size_t frame_size = tumovgm_frame_encoded_size(frame);
    if(output_capacity < frame_size) {
        return TumovgmCodecStatusOutputTooSmall;
    }

    memcpy(output, tumovgm_magic, sizeof(tumovgm_magic));
    output[TumovgmOffsetMajor] = frame->major;
    output[TumovgmOffsetMinor] = frame->minor;
    output[TumovgmOffsetKind] = (uint8_t)frame->kind;
    output[TumovgmOffsetFlags] = frame->flags;
    tumovgm_write_u16_le(output + TumovgmOffsetSequence, frame->sequence);
    tumovgm_write_u16_le(output + TumovgmOffsetMessage, frame->message);
    tumovgm_write_u16_le(output + TumovgmOffsetPayloadLength, frame->payload_length);
    if(frame->payload_length > 0) {
        memcpy(output + TumovgmFrameHeaderSize, frame->payload, frame->payload_length);
    }

    const uint16_t crc = tumovgm_crc16_ccitt_false(
        output + TumovgmOffsetMajor,
        (TumovgmFrameHeaderSize - TumovgmOffsetMajor) + frame->payload_length);
    tumovgm_write_u16_le(output + frame_size - TumovgmFrameCrcSize, crc);
    *written = frame_size;
    return TumovgmCodecStatusOk;
}

TumovgmCodecStatus tumovgm_frame_decode(
    const uint8_t* input,
    size_t input_length,
    TumovgmFrame* frame,
    size_t* consumed) {
    if(consumed != NULL) {
        *consumed = 0;
    }
    if(input == NULL || frame == NULL || consumed == NULL) {
        return TumovgmCodecStatusInvalidArgument;
    }
    if(input_length < sizeof(tumovgm_magic)) {
        if(tumovgm_magic_prefix_matches(input, input_length)) {
            return TumovgmCodecStatusNeedMore;
        }
        *consumed = tumovgm_resync_skip(input, input_length);
        return TumovgmCodecStatusBadMagic;
    }
    if(memcmp(input, tumovgm_magic, sizeof(tumovgm_magic)) != 0) {
        *consumed = tumovgm_resync_skip(input, input_length);
        return TumovgmCodecStatusBadMagic;
    }
    if(input_length < TumovgmFrameHeaderSize) {
        return TumovgmCodecStatusNeedMore;
    }

    const uint16_t payload_length = tumovgm_read_u16_le(input + TumovgmOffsetPayloadLength);
    if(payload_length > TUMOVGM_PROTOCOL_MAX_PAYLOAD) {
        *consumed = 1;
        return TumovgmCodecStatusPayloadTooLarge;
    }
    const size_t frame_size =
        TumovgmFrameHeaderSize + (size_t)payload_length + TumovgmFrameCrcSize;
    if(input_length < frame_size) {
        return TumovgmCodecStatusNeedMore;
    }

    const uint16_t expected_crc = tumovgm_crc16_ccitt_false(
        input + TumovgmOffsetMajor,
        (TumovgmFrameHeaderSize - TumovgmOffsetMajor) + payload_length);
    const uint16_t actual_crc =
        tumovgm_read_u16_le(input + frame_size - TumovgmFrameCrcSize);
    if(expected_crc != actual_crc) {
        *consumed = 1;
        return TumovgmCodecStatusBadCrc;
    }

    const TumovgmFrame decoded = {
        .major = input[TumovgmOffsetMajor],
        .minor = input[TumovgmOffsetMinor],
        .kind = (TumovgmFrameKind)input[TumovgmOffsetKind],
        .flags = input[TumovgmOffsetFlags],
        .sequence = tumovgm_read_u16_le(input + TumovgmOffsetSequence),
        .message = tumovgm_read_u16_le(input + TumovgmOffsetMessage),
        .payload_length = payload_length,
        .payload = payload_length == 0 ? NULL : input + TumovgmFrameHeaderSize,
    };
    if(!tumovgm_frame_semantics_valid(&decoded)) {
        *consumed = frame_size;
        return TumovgmCodecStatusBadSemantics;
    }

    *frame = decoded;
    *consumed = frame_size;
    return TumovgmCodecStatusOk;
}

TumovgmNegotiationStatus tumovgm_protocol_negotiate(
    uint8_t peer_major,
    uint8_t peer_minor,
    uint16_t peer_max_payload,
    TumovgmNegotiatedProtocol* negotiated) {
    if(negotiated == NULL || peer_max_payload == 0) {
        return TumovgmNegotiationStatusInvalidLimit;
    }
    if(peer_major != TUMOVGM_PROTOCOL_MAJOR) {
        return TumovgmNegotiationStatusMajorMismatch;
    }

    negotiated->minor = tumovgm_min_u8(peer_minor, TUMOVGM_PROTOCOL_MINOR);
    negotiated->max_payload = peer_max_payload < TUMOVGM_PROTOCOL_MAX_PAYLOAD ?
                                  peer_max_payload :
                                  TUMOVGM_PROTOCOL_MAX_PAYLOAD;
    return TumovgmNegotiationStatusOk;
}

void tumovgm_session_init(TumovgmSession* session) {
    if(session != NULL) {
        *session = (TumovgmSession){.state = TumovgmSessionStateDisconnected};
    }
}

TumovgmSessionResult tumovgm_session_connect(TumovgmSession* session) {
    if(session == NULL || session->state != TumovgmSessionStateDisconnected) {
        return TumovgmSessionResultBadState;
    }
    session->state = TumovgmSessionStateNegotiating;
    return TumovgmSessionResultOk;
}

TumovgmSessionResult tumovgm_session_ready(TumovgmSession* session) {
    if(session == NULL || session->state != TumovgmSessionStateNegotiating) {
        return TumovgmSessionResultBadState;
    }
    session->state = TumovgmSessionStateReady;
    return TumovgmSessionResultOk;
}

TumovgmSessionResult tumovgm_session_open(TumovgmSession* session, uint32_t session_id) {
    if(session == NULL || session_id == 0 || session->state != TumovgmSessionStateReady) {
        return TumovgmSessionResultBadState;
    }
    session->active_id = session_id;
    session->state = TumovgmSessionStateActive;
    return TumovgmSessionResultOk;
}

TumovgmSessionResult tumovgm_session_close(TumovgmSession* session, uint32_t session_id) {
    if(session == NULL || session_id == 0) {
        return TumovgmSessionResultBadState;
    }
    if(session->state == TumovgmSessionStateReady && session->last_closed_id == session_id) {
        return TumovgmSessionResultAlreadyClosed;
    }
    if(session->state != TumovgmSessionStateActive) {
        return TumovgmSessionResultBadState;
    }
    if(session->active_id != session_id) {
        return TumovgmSessionResultNotOwner;
    }

    session->last_closed_id = session->active_id;
    session->active_id = 0;
    session->state = TumovgmSessionStateReady;
    return TumovgmSessionResultOk;
}

bool tumovgm_session_disconnect(TumovgmSession* session) {
    if(session == NULL) {
        return false;
    }
    const bool closed_active_session = session->state == TumovgmSessionStateActive;
    if(closed_active_session) {
        session->last_closed_id = session->active_id;
    }
    session->active_id = 0;
    session->state = TumovgmSessionStateDisconnected;
    return closed_active_session;
}

void tumovgm_stream_window_init(TumovgmStreamWindow* window, uint16_t max_queued) {
    if(window != NULL) {
        *window = (TumovgmStreamWindow){.max_queued = max_queued};
    }
}

bool tumovgm_stream_grant(TumovgmStreamWindow* window, uint16_t credits) {
    if(window == NULL || credits > (uint16_t)(UINT16_MAX - window->credits)) {
        return false;
    }
    window->credits = (uint16_t)(window->credits + credits);
    return true;
}

bool tumovgm_stream_consume(TumovgmStreamWindow* window) {
    if(window == NULL || window->credits == 0) {
        return false;
    }
    window->credits--;
    return true;
}

bool tumovgm_stream_queue_push(TumovgmStreamWindow* window) {
    if(window == NULL || window->queued >= window->max_queued) {
        return false;
    }
    window->queued++;
    return true;
}

bool tumovgm_stream_queue_pop(TumovgmStreamWindow* window) {
    if(window == NULL || window->queued == 0) {
        return false;
    }
    window->queued--;
    return true;
}
