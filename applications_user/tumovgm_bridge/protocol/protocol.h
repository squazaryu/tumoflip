#ifndef TUMOVGM_PROTOCOL_H
#define TUMOVGM_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TumovgmFrameHeaderSize = 14,
    TumovgmFrameCrcSize = 2,
    TumovgmFrameMaxSize =
        TumovgmFrameHeaderSize + TUMOVGM_PROTOCOL_MAX_PAYLOAD + TumovgmFrameCrcSize,
    TumovgmFrameKnownFlags = TumovgmFrameFlagMore | TumovgmFrameFlagAckRequired,
};

typedef struct TumovgmFrame {
    uint8_t major;
    uint8_t minor;
    TumovgmFrameKind kind;
    uint8_t flags;
    uint16_t sequence;
    uint16_t message;
    uint16_t payload_length;
    const uint8_t* payload;
} TumovgmFrame;

typedef enum TumovgmCodecStatus {
    TumovgmCodecStatusOk = 0,
    TumovgmCodecStatusNeedMore,
    TumovgmCodecStatusInvalidArgument,
    TumovgmCodecStatusBadMagic,
    TumovgmCodecStatusPayloadTooLarge,
    TumovgmCodecStatusBadCrc,
    TumovgmCodecStatusBadSemantics,
    TumovgmCodecStatusOutputTooSmall,
} TumovgmCodecStatus;

typedef enum TumovgmNegotiationStatus {
    TumovgmNegotiationStatusOk = 0,
    TumovgmNegotiationStatusMajorMismatch,
    TumovgmNegotiationStatusInvalidLimit,
} TumovgmNegotiationStatus;

typedef struct TumovgmNegotiatedProtocol {
    uint8_t minor;
    uint16_t max_payload;
} TumovgmNegotiatedProtocol;

typedef enum TumovgmSessionState {
    TumovgmSessionStateDisconnected = 0,
    TumovgmSessionStateNegotiating,
    TumovgmSessionStateReady,
    TumovgmSessionStateActive,
} TumovgmSessionState;

typedef enum TumovgmSessionResult {
    TumovgmSessionResultOk = 0,
    TumovgmSessionResultAlreadyClosed,
    TumovgmSessionResultBadState,
    TumovgmSessionResultNotOwner,
} TumovgmSessionResult;

typedef struct TumovgmSession {
    TumovgmSessionState state;
    uint32_t active_id;
    uint32_t last_closed_id;
} TumovgmSession;

typedef struct TumovgmStreamWindow {
    uint16_t credits;
    uint16_t queued;
    uint16_t max_queued;
} TumovgmStreamWindow;

uint16_t tumovgm_crc16_ccitt_false(const uint8_t* data, size_t length);

size_t tumovgm_frame_encoded_size(const TumovgmFrame* frame);

TumovgmCodecStatus tumovgm_frame_encode(
    const TumovgmFrame* frame,
    uint8_t* output,
    size_t output_capacity,
    size_t* written);

TumovgmCodecStatus tumovgm_frame_decode(
    const uint8_t* input,
    size_t input_length,
    TumovgmFrame* frame,
    size_t* consumed);

TumovgmNegotiationStatus tumovgm_protocol_negotiate(
    uint8_t peer_major,
    uint8_t peer_minor,
    uint16_t peer_max_payload,
    TumovgmNegotiatedProtocol* negotiated);

void tumovgm_session_init(TumovgmSession* session);
TumovgmSessionResult tumovgm_session_connect(TumovgmSession* session);
TumovgmSessionResult tumovgm_session_ready(TumovgmSession* session);
TumovgmSessionResult tumovgm_session_open(TumovgmSession* session, uint32_t session_id);
TumovgmSessionResult tumovgm_session_close(TumovgmSession* session, uint32_t session_id);
bool tumovgm_session_disconnect(TumovgmSession* session);

void tumovgm_stream_window_init(TumovgmStreamWindow* window, uint16_t max_queued);
bool tumovgm_stream_grant(TumovgmStreamWindow* window, uint16_t credits);
bool tumovgm_stream_consume(TumovgmStreamWindow* window);
bool tumovgm_stream_queue_push(TumovgmStreamWindow* window);
bool tumovgm_stream_queue_pop(TumovgmStreamWindow* window);

#ifdef __cplusplus
}
#endif

#endif
