#!/usr/bin/env python3
"""Reference codec for the tumoflip FAB2 wire protocol."""

from __future__ import annotations

from dataclasses import dataclass
import struct


MAGIC = b"FAB2"
HEADER = struct.Struct("<4sBBBBBBHI")
APP_ID_MAX = 32
COMMAND_MAX = 32
PAYLOAD_MAX = 160
FRAME_MAX = 244
FLAGS_MASK = 0x07


@dataclass(frozen=True)
class Frame:
    app_id: str
    command: str
    request_id: int
    flags: int = 0
    chunk_index: int = 0
    chunk_count: int = 1
    payload: bytes = b""


def encode(frame: Frame) -> bytes:
    app_id = frame.app_id.encode("utf-8")
    command = frame.command.encode("utf-8")
    if not 1 <= len(app_id) <= APP_ID_MAX:
        raise ValueError("app_id must encode to 1..32 bytes")
    if not 1 <= len(command) <= COMMAND_MAX:
        raise ValueError("command must encode to 1..32 bytes")
    if len(frame.payload) > PAYLOAD_MAX:
        raise ValueError("payload exceeds 160 bytes")
    if not 1 <= frame.chunk_count <= 255:
        raise ValueError("chunk_count must be 1..255")
    if not 0 <= frame.chunk_index < frame.chunk_count:
        raise ValueError("chunk_index is outside chunk_count")
    if not 0 <= frame.request_id <= 0xFFFFFFFF:
        raise ValueError("request_id must fit uint32")
    if not 0 <= frame.flags <= FLAGS_MASK:
        raise ValueError("flags contain unsupported bits")

    header = HEADER.pack(
        MAGIC,
        frame.flags,
        len(app_id),
        len(command),
        frame.chunk_index,
        frame.chunk_count,
        0,
        len(frame.payload),
        frame.request_id,
    )
    encoded = header + app_id + command + frame.payload
    if len(encoded) > FRAME_MAX:
        raise ValueError("encoded frame exceeds the GATT frame limit")
    return encoded


def decode(data: bytes) -> Frame:
    if len(data) < HEADER.size:
        raise ValueError("truncated FAB2 header")
    (
        magic,
        flags,
        app_len,
        command_len,
        chunk_index,
        chunk_count,
        reserved,
        payload_len,
        request_id,
    ) = HEADER.unpack_from(data)
    if magic != MAGIC or reserved != 0 or flags & ~FLAGS_MASK:
        raise ValueError("invalid FAB2 header")
    expected = HEADER.size + app_len + command_len + payload_len
    if len(data) != expected:
        raise ValueError("FAB2 frame length mismatch")
    if not 1 <= app_len <= APP_ID_MAX or not 1 <= command_len <= COMMAND_MAX:
        raise ValueError("invalid identifier length")
    if payload_len > PAYLOAD_MAX or not 1 <= chunk_count <= 255:
        raise ValueError("invalid payload or chunk count")
    if chunk_index >= chunk_count:
        raise ValueError("invalid chunk index")

    offset = HEADER.size
    app_id = data[offset : offset + app_len].decode("utf-8")
    offset += app_len
    command = data[offset : offset + command_len].decode("utf-8")
    offset += command_len
    return Frame(
        app_id=app_id,
        command=command,
        request_id=request_id,
        flags=flags,
        chunk_index=chunk_index,
        chunk_count=chunk_count,
        payload=data[offset:],
    )
