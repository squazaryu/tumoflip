#!/usr/bin/env python3
"""Reference helpers for Tumoflip App Bridge v3 session payloads."""

from __future__ import annotations

from collections.abc import Mapping
import re


OWNER_MAX = 24
SESSION_LEN = 8
TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
SESSION_RE = re.compile(r"^[0-9A-F]{8}$")


def _validate_token(name: str, value: str, max_len: int) -> str:
    if not 1 <= len(value.encode("ascii", "strict")) <= max_len:
        raise ValueError(f"{name} must be 1..{max_len} ASCII bytes")
    if not TOKEN_RE.fullmatch(value):
        raise ValueError(f"{name} contains unsupported characters")
    return value


def _validate_session(session: str) -> str:
    normalized = session.upper()
    if not SESSION_RE.fullmatch(normalized):
        raise ValueError("session must be 8 uppercase hex characters")
    return normalized


def encode_fields(fields: Mapping[str, str | int]) -> bytes:
    parts: list[str] = []
    for key, value in fields.items():
        _validate_token("key", key, OWNER_MAX)
        text = str(value)
        if text == "":
            raise ValueError("field values must not be empty")
        parts.append(f"{key}={text}")
    return ";".join(parts).encode("ascii")


def decode_fields(payload: bytes | str) -> dict[str, str]:
    text = payload.decode("ascii") if isinstance(payload, bytes) else payload
    if text == "":
        return {}

    fields: dict[str, str] = {}
    for part in text.split(";"):
        if "=" not in part:
            raise ValueError("field is missing '='")
        key, value = part.split("=", 1)
        if key in fields:
            raise ValueError(f"duplicate field: {key}")
        _validate_token("key", key, OWNER_MAX)
        if value == "":
            raise ValueError(f"empty field: {key}")
        fields[key] = value
    return fields


def hello_payload(owner: str) -> bytes:
    return encode_fields(
        {
            "owner": _validate_token("owner", owner, OWNER_MAX),
        }
    )


def session_payload(session: str) -> bytes:
    return encode_fields({"session": _validate_session(session)})


def parse_hello_response(payload: bytes | str) -> dict[str, str]:
    fields = decode_fields(payload)
    _validate_session(fields.get("sid", ""))
    return fields
