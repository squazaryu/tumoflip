#!/usr/bin/env python3
"""Infer a bounded receive-only pulse-pair profile from Flipper RAW captures."""

from __future__ import annotations

import argparse
import hashlib
import re
import statistics
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Iterable, Sequence


PROFILE_FILETYPE = "Tumo Protocol Profile"
PROFILE_VERSION = 1
PROFILE_MINIMUM_API = 88
MAX_CAPTURE_BYTES = 1024 * 1024
MAX_PROFILE_BYTES = 2048
MAX_CAPTURES = 16
MAX_PULSES_PER_CAPTURE = 4096
MAX_FRAMES = 64
MAX_PREFIX_PULSES = 24
MAX_BITS = 64
MIN_BITS = 4
MIN_DURATION_US = 40
MAX_DURATION_US = 1_000_000


class ProtocolCompilerError(RuntimeError):
    pass


@dataclass(frozen=True)
class RawCapture:
    path: Path
    frequency_hz: int
    pulses: tuple[int, ...]


@dataclass(frozen=True)
class ProtocolProfile:
    name: str
    profile_id: str
    frequency_hz: int
    prefix: tuple[int, ...]
    bit_count: int
    zero_high_us: int
    zero_low_us: int
    one_high_us: int
    one_low_us: int
    tolerance_percent: int
    stable_mask: int
    stable_value: int
    variable_mask: int
    uncertain_mask: int
    checksum: str
    checksum_candidates: tuple[str, ...]
    confidence: int
    ambiguity: str
    training_captures: int
    training_frames: int
    inverted: bool = False
    receive_only: bool = True
    review_required: bool = True


@dataclass(frozen=True)
class DecodeResult:
    value: int
    bit_count: int
    start_offset: int
    stable_match: bool
    checksum_match: bool


_INTEGER = re.compile(r"^[+-]?\d+$")


def _parse_uint_field(lines: Sequence[str], key: str, required: bool = True) -> int:
    prefix = f"{key}:"
    for line in lines:
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
            if not value.isdigit():
                raise ProtocolCompilerError(f"{key} is not an unsigned integer")
            return int(value)
    if required:
        raise ProtocolCompilerError(f"missing {key}")
    return 0


def parse_sub_capture(path: Path) -> RawCapture:
    try:
        size = path.stat().st_size
    except OSError as error:
        raise ProtocolCompilerError(f"cannot stat capture: {path}") from error
    if size == 0 or size > MAX_CAPTURE_BYTES:
        raise ProtocolCompilerError(f"capture size is outside 1..{MAX_CAPTURE_BYTES}: {path}")

    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ProtocolCompilerError(f"cannot read capture as UTF-8: {path}") from error

    lines = text.splitlines()
    if not lines or lines[0].strip() != "Filetype: Flipper SubGhz RAW File":
        raise ProtocolCompilerError(f"not a Flipper SubGhz RAW file: {path}")
    if not any(line.strip() == "Protocol: RAW" for line in lines):
        raise ProtocolCompilerError(f"capture protocol is not RAW: {path}")

    frequency_hz = _parse_uint_field(lines, "Frequency", required=False)
    pulses: list[int] = []
    for line in lines:
        if not line.startswith("RAW_Data:"):
            continue
        payload = line.split(":", 1)[1].strip()
        if not payload:
            raise ProtocolCompilerError(f"empty RAW_Data line: {path}")
        for token in payload.replace(",", " ").split():
            if not _INTEGER.fullmatch(token):
                raise ProtocolCompilerError(f"invalid RAW duration {token!r}: {path}")
            value = int(token)
            if value == 0 or abs(value) < MIN_DURATION_US or abs(value) > MAX_DURATION_US:
                raise ProtocolCompilerError(f"RAW duration outside bounds: {value}")
            pulses.append(value)
            if len(pulses) > MAX_PULSES_PER_CAPTURE:
                raise ProtocolCompilerError(
                    f"capture exceeds {MAX_PULSES_PER_CAPTURE} pulses: {path}"
                )
    if not pulses:
        raise ProtocolCompilerError(f"capture has no RAW_Data: {path}")
    return RawCapture(path=path, frequency_hz=frequency_hz, pulses=tuple(pulses))


def _median_int(values: Iterable[int]) -> int:
    materialized = list(values)
    if not materialized:
        raise ProtocolCompilerError("cannot take median of an empty timing set")
    return int(round(statistics.median(materialized)))


def _separator_threshold(pulses: Sequence[int]) -> int:
    magnitudes = sorted(abs(value) for value in pulses)
    lower = magnitudes[: max(1, len(magnitudes) * 3 // 4)]
    return max(2500, _median_int(lower) * 6)


def split_frames(capture: RawCapture) -> list[tuple[int, ...]]:
    threshold = _separator_threshold(capture.pulses)
    frames: list[tuple[int, ...]] = []
    current: list[int] = []
    for pulse in capture.pulses:
        if pulse < 0 and abs(pulse) >= threshold:
            if len(current) >= MIN_BITS * 2:
                frames.append(tuple(current))
            current = []
            if len(frames) >= MAX_FRAMES:
                break
        else:
            current.append(pulse)
    if len(current) >= MIN_BITS * 2 and len(frames) < MAX_FRAMES:
        frames.append(tuple(current))
    if not frames:
        raise ProtocolCompilerError(f"no bounded frame found in {capture.path}")
    return frames


def _modal_frame_length(frames: Sequence[tuple[int, ...]]) -> int:
    counts: dict[int, int] = {}
    for frame in frames:
        counts[len(frame)] = counts.get(len(frame), 0) + 1
    return min(counts, key=lambda length: (-counts[length], length))


def _kmeans_two(values: Sequence[int]) -> tuple[int, int]:
    if len(values) < 4:
        raise ProtocolCompilerError("not enough timings to infer short/long clusters")
    ordered = sorted(values)
    short = ordered[len(ordered) // 4]
    long = ordered[(len(ordered) * 3) // 4]
    for _ in range(12):
        low_cluster: list[int] = []
        high_cluster: list[int] = []
        for value in ordered:
            if abs(value - short) <= abs(value - long):
                low_cluster.append(value)
            else:
                high_cluster.append(value)
        if not low_cluster or not high_cluster:
            raise ProtocolCompilerError("timing clusters collapsed")
        updated_short = _median_int(low_cluster)
        updated_long = _median_int(high_cluster)
        if (updated_short, updated_long) == (short, long):
            break
        short, long = updated_short, updated_long
    short, long = sorted((short, long))
    if long * 100 < short * 145:
        raise ProtocolCompilerError("short and long timing clusters are not separated")
    return short, long


def _timing_class(value: int, short: int, long: int) -> int:
    magnitude = abs(value)
    return 0 if abs(magnitude - short) <= abs(magnitude - long) else 1


def _decode_candidate(
    frames: Sequence[tuple[int, ...]], offset: int, short: int, long: int
) -> tuple[list[list[int]], bool] | None:
    decoded: list[list[int]] = []
    polarity_votes = 0
    pair_count = 0
    for frame in frames:
        payload = frame[offset:]
        if len(payload) % 2 or not (MIN_BITS <= len(payload) // 2 <= MAX_BITS):
            return None
        bits: list[int] = []
        for index in range(0, len(payload), 2):
            first, second = payload[index], payload[index + 1]
            if first == 0 or second == 0 or (first > 0) == (second > 0):
                return None
            if first > 0:
                polarity_votes += 1
            pair_count += 1
            first_class = _timing_class(first, short, long)
            second_class = _timing_class(second, short, long)
            if first_class == second_class:
                return None
            bits.append(0 if (first_class, second_class) == (0, 1) else 1)
        decoded.append(bits)
    inverted = polarity_votes * 2 < pair_count
    if polarity_votes not in (0, pair_count):
        return None
    return decoded, inverted


def _bits_to_int(bits: Sequence[int]) -> int:
    value = 0
    for bit in bits:
        value = (value << 1) | bit
    return value


def _checksum_matches(bits: Sequence[int], candidate: str) -> bool:
    if candidate in ("parity-even-last", "parity-odd-last"):
        if len(bits) < 2:
            return False
        parity = sum(bits[:-1]) & 1
        expected = parity if candidate == "parity-even-last" else parity ^ 1
        return bits[-1] == expected
    if candidate in ("xor8-last", "sum8-last"):
        if len(bits) < 16 or len(bits) % 8:
            return False
        value = _bits_to_int(bits)
        byte_count = len(bits) // 8
        octets = [
            (value >> ((byte_count - index - 1) * 8)) & 0xFF for index in range(byte_count)
        ]
        if candidate == "xor8-last":
            expected = 0
            for octet in octets[:-1]:
                expected ^= octet
        else:
            expected = sum(octets[:-1]) & 0xFF
        return octets[-1] == expected
    return candidate == "none"


def _checksum_candidates(decoded: Sequence[Sequence[int]]) -> tuple[str, ...]:
    candidates = ("parity-even-last", "parity-odd-last", "xor8-last", "sum8-last")
    distinct = {_bits_to_int(bits) for bits in decoded}
    if len(distinct) < 3:
        return ()
    return tuple(
        candidate
        for candidate in candidates
        if all(_checksum_matches(bits, candidate) for bits in decoded)
    )


def _profile_id(profile: ProtocolProfile) -> str:
    identity = "|".join(
        (
            profile.name,
            str(profile.frequency_hz),
            ",".join(str(value) for value in profile.prefix),
            str(profile.bit_count),
            str(profile.zero_high_us),
            str(profile.zero_low_us),
            str(profile.one_high_us),
            str(profile.one_low_us),
            f"{profile.stable_mask:016X}",
            f"{profile.stable_value:016X}",
            profile.checksum,
        )
    )
    return hashlib.sha256(identity.encode("ascii")).hexdigest()[:16]


def compile_profile(name: str, captures: Sequence[RawCapture]) -> ProtocolProfile:
    if not name or len(name.encode("utf-8")) > 31:
        raise ProtocolCompilerError("profile name must contain 1..31 UTF-8 bytes")
    if not (2 <= len(captures) <= MAX_CAPTURES):
        raise ProtocolCompilerError(f"compiler requires 2..{MAX_CAPTURES} captures")

    all_frames = [frame for capture in captures for frame in split_frames(capture)]
    if len(all_frames) > MAX_FRAMES:
        all_frames = all_frames[:MAX_FRAMES]
    modal_length = _modal_frame_length(all_frames)
    frames = [frame for frame in all_frames if len(frame) == modal_length]
    if len(frames) < 2:
        raise ProtocolCompilerError("fewer than two aligned frames remain")

    threshold = min(_separator_threshold(capture.pulses) for capture in captures)
    timings = [abs(pulse) for frame in frames for pulse in frame if abs(pulse) < threshold]
    short, long = _kmeans_two(timings)

    candidate = None
    maximum_offset = min(MAX_PREFIX_PULSES, modal_length - MIN_BITS * 2)
    for offset in range(maximum_offset + 1):
        if (modal_length - offset) % 2:
            continue
        decoded_candidate = _decode_candidate(frames, offset, short, long)
        if decoded_candidate is not None:
            candidate = (offset, *decoded_candidate)
            break
    if candidate is None:
        raise ProtocolCompilerError("no bounded pulse-pair hypothesis fits every aligned frame")

    offset, decoded, inverted = candidate
    bit_count = len(decoded[0])
    values = [_bits_to_int(bits) for bits in decoded]
    width_mask = (1 << bit_count) - 1 if bit_count < 64 else (1 << 64) - 1
    stable_mask = width_mask
    stable_value = values[0]
    for value in values[1:]:
        stable_mask &= ~(stable_value ^ value) & width_mask
    stable_value &= stable_mask
    variable_mask = width_mask ^ stable_mask

    zero_high: list[int] = []
    zero_low: list[int] = []
    one_high: list[int] = []
    one_low: list[int] = []
    timing_errors: list[float] = []
    for frame, bits in zip(frames, decoded, strict=True):
        payload = frame[offset:]
        for index, bit in enumerate(bits):
            first, second = payload[index * 2 : index * 2 + 2]
            high = abs(first if not inverted else second)
            low = abs(second if not inverted else first)
            if bit == 0:
                zero_high.append(high)
                zero_low.append(low)
            else:
                one_high.append(high)
                one_low.append(low)
    if not zero_high or not one_high:
        raise ProtocolCompilerError("both zero and one symbols are required")

    timing_centers = (
        _median_int(zero_high),
        _median_int(zero_low),
        _median_int(one_high),
        _median_int(one_low),
    )
    for frame, bits in zip(frames, decoded, strict=True):
        payload = frame[offset:]
        for index, bit in enumerate(bits):
            first, second = payload[index * 2 : index * 2 + 2]
            actual_high = abs(first if not inverted else second)
            actual_low = abs(second if not inverted else first)
            expected_high = timing_centers[0] if bit == 0 else timing_centers[2]
            expected_low = timing_centers[1] if bit == 0 else timing_centers[3]
            timing_errors.extend(
                (
                    abs(actual_high - expected_high) * 100.0 / expected_high,
                    abs(actual_low - expected_low) * 100.0 / expected_low,
                )
            )
    tolerance = max(15, min(35, int(round(max(timing_errors, default=0.0) + 8.0))))

    prefix = tuple(
        _median_int(frame[index] for frame in frames) for index in range(offset)
    )
    checksum_candidates = _checksum_candidates(decoded)
    checksum = checksum_candidates[0] if len(checksum_candidates) == 1 else "none"
    derived_mask = 0
    if checksum in ("parity-even-last", "parity-odd-last"):
        derived_mask = 1
    elif checksum in ("xor8-last", "sum8-last"):
        derived_mask = 0xFF
    stable_mask &= ~derived_mask & width_mask
    stable_value &= stable_mask
    variable_mask |= derived_mask
    ambiguity_parts: list[str] = []
    if not checksum_candidates:
        ambiguity_parts.append("checksum-unknown")
    elif len(checksum_candidates) > 1:
        ambiguity_parts.append("checksum-ambiguous")
    if variable_mask == 0:
        ambiguity_parts.append("no-changing-fields")
    ambiguity = ",".join(ambiguity_parts) if ambiguity_parts else "none"

    cluster_separation = min(20, max(0, (long - short) * 20 // max(long, 1)))
    confidence = 50 + cluster_separation + min(12, len(frames) * 2)
    confidence += 6 if checksum != "none" else 0
    confidence -= 10 * len(ambiguity_parts)
    confidence = max(1, min(99, confidence))

    frequencies = {capture.frequency_hz for capture in captures if capture.frequency_hz}
    frequency_hz = frequencies.pop() if len(frequencies) == 1 else 0
    if frequency_hz == 0 and "frequency-ambiguous" not in ambiguity_parts:
        ambiguity_parts.append("frequency-ambiguous")
        ambiguity = ",".join(ambiguity_parts)

    profile = ProtocolProfile(
        name=name,
        profile_id="pending",
        frequency_hz=frequency_hz,
        prefix=prefix,
        bit_count=bit_count,
        zero_high_us=timing_centers[0],
        zero_low_us=timing_centers[1],
        one_high_us=timing_centers[2],
        one_low_us=timing_centers[3],
        tolerance_percent=tolerance,
        stable_mask=stable_mask,
        stable_value=stable_value,
        variable_mask=variable_mask,
        uncertain_mask=0,
        checksum=checksum,
        checksum_candidates=checksum_candidates,
        confidence=confidence,
        ambiguity=ambiguity,
        training_captures=len(captures),
        training_frames=len(frames),
        inverted=inverted,
    )
    return replace(profile, profile_id=_profile_id(profile))


def _timing_error_percent(actual: int, expected: int) -> int:
    if actual == 0 or expected == 0 or (actual > 0) != (expected > 0):
        return 10_000
    return abs(abs(actual) - abs(expected)) * 100 // abs(expected)


def _decode_at(profile: ProtocolProfile, pulses: Sequence[int], offset: int) -> DecodeResult | None:
    cursor = offset
    for expected in profile.prefix:
        if cursor >= len(pulses):
            return None
        if _timing_error_percent(pulses[cursor], expected) > profile.tolerance_percent:
            return None
        cursor += 1

    value = 0
    bits: list[int] = []
    for _ in range(profile.bit_count):
        if cursor + 1 >= len(pulses):
            return None
        first, second = pulses[cursor], pulses[cursor + 1]
        cursor += 2
        high = second if profile.inverted else first
        low = first if profile.inverted else second
        zero_error = max(
            _timing_error_percent(high, profile.zero_high_us),
            _timing_error_percent(low, -profile.zero_low_us),
        )
        one_error = max(
            _timing_error_percent(high, profile.one_high_us),
            _timing_error_percent(low, -profile.one_low_us),
        )
        best = min(zero_error, one_error)
        if best > profile.tolerance_percent or zero_error == one_error:
            return None
        bit = 0 if zero_error < one_error else 1
        bits.append(bit)
        value = (value << 1) | bit
    return DecodeResult(
        value=value,
        bit_count=profile.bit_count,
        start_offset=offset,
        stable_match=(value & profile.stable_mask) == profile.stable_value,
        checksum_match=_checksum_matches(bits, profile.checksum),
    )


def decode_capture(profile: ProtocolProfile, capture: RawCapture) -> DecodeResult:
    if (
        profile.frequency_hz > 0
        and capture.frequency_hz > 0
        and profile.frequency_hz != capture.frequency_hz
    ):
        raise ProtocolCompilerError("capture frequency does not match the profile")
    required = len(profile.prefix) + profile.bit_count * 2
    if required > len(capture.pulses):
        raise ProtocolCompilerError("capture is shorter than the profile")
    best_failure: DecodeResult | None = None
    for offset in range(len(capture.pulses) - required + 1):
        result = _decode_at(profile, capture.pulses, offset)
        if result is None:
            continue
        if result.stable_match and result.checksum_match:
            return result
        if best_failure is None or (
            result.stable_match and not best_failure.stable_match
        ):
            best_failure = result
    if best_failure is not None:
        return best_failure
    raise ProtocolCompilerError(f"profile did not decode {capture.path}")


def profile_to_text(profile: ProtocolProfile) -> str:
    width = max(1, (profile.bit_count + 3) // 4)
    checksum_candidates = ",".join(profile.checksum_candidates) or "none"
    prefix = " ".join(str(value) for value in profile.prefix)
    return "\n".join(
        (
            f"Filetype: {PROFILE_FILETYPE}",
            f"Version: {PROFILE_VERSION}",
            f"Name: {profile.name}",
            f"Profile ID: {profile.profile_id}",
            f"Minimum API: {PROFILE_MINIMUM_API}",
            "Encoding: pulse-pair",
            f"Polarity: {'inverted' if profile.inverted else 'normal'}",
            f"Frequency: {profile.frequency_hz}",
            f"Tolerance: {profile.tolerance_percent}",
            f"Preamble count: {len(profile.prefix)}",
            f"Preamble: {prefix}",
            f"Bit count: {profile.bit_count}",
            f"Zero high: {profile.zero_high_us}",
            f"Zero low: {profile.zero_low_us}",
            f"One high: {profile.one_high_us}",
            f"One low: {profile.one_low_us}",
            f"Stable mask: 0x{profile.stable_mask:0{width}X}",
            f"Stable value: 0x{profile.stable_value:0{width}X}",
            f"Variable mask: 0x{profile.variable_mask:0{width}X}",
            f"Uncertain mask: 0x{profile.uncertain_mask:0{width}X}",
            f"Checksum: {profile.checksum}",
            f"Checksum candidates: {checksum_candidates}",
            f"Confidence: {profile.confidence}",
            f"Ambiguity: {profile.ambiguity}",
            f"Training captures: {profile.training_captures}",
            f"Training frames: {profile.training_frames}",
            "Receive only: true",
            "Review required: true",
            "",
        )
    )


def _parse_profile_text(path: Path) -> ProtocolProfile:
    try:
        size = path.stat().st_size
    except OSError as error:
        raise ProtocolCompilerError(f"cannot stat profile: {path}") from error
    if size == 0 or size > MAX_PROFILE_BYTES:
        raise ProtocolCompilerError(
            f"profile size is outside 1..{MAX_PROFILE_BYTES}: {path}"
        )
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ProtocolCompilerError(f"cannot read profile as UTF-8: {path}") from error
    lines = text.splitlines()
    fields: dict[str, str] = {}
    for line in lines:
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        if key in fields:
            raise ProtocolCompilerError(f"duplicate profile field: {key}")
        fields[key] = value.strip()
    if fields.get("Filetype") != PROFILE_FILETYPE or fields.get("Version") != "1":
        raise ProtocolCompilerError("unsupported protocol profile")
    required_fields = (
        "Name",
        "Profile ID",
        "Minimum API",
        "Encoding",
        "Polarity",
        "Frequency",
        "Tolerance",
        "Preamble count",
        "Bit count",
        "Zero high",
        "Zero low",
        "One high",
        "One low",
        "Stable mask",
        "Stable value",
        "Variable mask",
        "Uncertain mask",
        "Checksum",
        "Confidence",
        "Ambiguity",
        "Training captures",
        "Training frames",
        "Receive only",
        "Review required",
    )
    missing = [field for field in required_fields if field not in fields]
    if missing:
        raise ProtocolCompilerError(f"profile is missing {', '.join(missing)}")
    if not (1 <= len(fields["Name"].encode("utf-8")) <= 31):
        raise ProtocolCompilerError("profile name must contain 1..31 UTF-8 bytes")
    if not re.fullmatch(r"[0-9A-Fa-f]{16}", fields["Profile ID"]):
        raise ProtocolCompilerError("profile ID must be 16 hexadecimal characters")
    if fields["Minimum API"] != str(PROFILE_MINIMUM_API):
        raise ProtocolCompilerError("profile API does not match this compiler")
    if fields["Encoding"] != "pulse-pair":
        raise ProtocolCompilerError("unsupported profile encoding")
    if fields["Polarity"] not in ("normal", "inverted"):
        raise ProtocolCompilerError("invalid profile polarity")
    if fields["Receive only"] != "true" or fields["Review required"] != "true":
        raise ProtocolCompilerError("profile is missing receive-only review gates")
    if not (1 <= len(fields["Ambiguity"].encode("utf-8")) <= 63):
        raise ProtocolCompilerError("profile ambiguity must contain 1..63 UTF-8 bytes")

    def parse_integer(field: str, minimum: int, maximum: int, base: int = 10) -> int:
        try:
            value = int(fields[field], base)
        except ValueError as error:
            raise ProtocolCompilerError(f"invalid integer in {field}") from error
        if not minimum <= value <= maximum:
            raise ProtocolCompilerError(f"{field} is outside {minimum}..{maximum}")
        return value

    prefix_count = parse_integer("Preamble count", 0, MAX_PREFIX_PULSES)
    try:
        prefix = tuple(int(value) for value in fields.get("Preamble", "").split())
    except ValueError as error:
        raise ProtocolCompilerError("invalid profile preamble") from error
    if len(prefix) != prefix_count:
        raise ProtocolCompilerError("profile preamble count mismatch")
    if any(
        value == 0 or abs(value) < MIN_DURATION_US or abs(value) > MAX_DURATION_US
        for value in prefix
    ):
        raise ProtocolCompilerError("profile preamble duration is outside bounds")
    bit_count = parse_integer("Bit count", MIN_BITS, MAX_BITS)
    tolerance = parse_integer("Tolerance", 5, 50)
    timings = {
        field: parse_integer(field, MIN_DURATION_US, MAX_DURATION_US)
        for field in ("Zero high", "Zero low", "One high", "One low")
    }
    confidence = parse_integer("Confidence", 0, 100)
    parse_integer("Training captures", 1, MAX_CAPTURES)
    parse_integer("Training frames", 1, MAX_FRAMES)
    frequency = parse_integer("Frequency", 0, 1_000_000_000)
    masks = {
        field: parse_integer(field, 0, (1 << 64) - 1, base=0)
        for field in ("Stable mask", "Stable value", "Variable mask", "Uncertain mask")
    }
    width_mask = (1 << bit_count) - 1
    if (
        masks["Stable mask"] & ~width_mask
        or masks["Stable value"] & ~masks["Stable mask"]
        or masks["Variable mask"] & ~width_mask
        or masks["Uncertain mask"] & ~width_mask
        or masks["Stable mask"] & masks["Variable mask"]
        or masks["Stable mask"] & masks["Uncertain mask"]
        or masks["Variable mask"] & masks["Uncertain mask"]
        or (
            masks["Stable mask"]
            | masks["Variable mask"]
            | masks["Uncertain mask"]
        )
        != width_mask
    ):
        raise ProtocolCompilerError("profile masks are unsafe")
    allowed_checksums = {
        "none",
        "parity-even-last",
        "parity-odd-last",
        "xor8-last",
        "sum8-last",
    }
    if fields["Checksum"] not in allowed_checksums:
        raise ProtocolCompilerError("unsupported checksum")
    if fields["Checksum"] in ("xor8-last", "sum8-last") and (
        bit_count < 16 or bit_count % 8
    ):
        raise ProtocolCompilerError("byte checksum requires a whole number of bytes")
    candidates = tuple(
        value for value in fields.get("Checksum candidates", "none").split(",") if value != "none"
    )
    if any(candidate not in allowed_checksums - {"none"} for candidate in candidates):
        raise ProtocolCompilerError("unsupported checksum candidate")
    return ProtocolProfile(
        name=fields["Name"],
        profile_id=fields["Profile ID"],
        frequency_hz=frequency,
        prefix=prefix,
        bit_count=bit_count,
        zero_high_us=timings["Zero high"],
        zero_low_us=timings["Zero low"],
        one_high_us=timings["One high"],
        one_low_us=timings["One low"],
        tolerance_percent=tolerance,
        stable_mask=masks["Stable mask"],
        stable_value=masks["Stable value"],
        variable_mask=masks["Variable mask"],
        uncertain_mask=masks["Uncertain mask"],
        checksum=fields["Checksum"],
        checksum_candidates=candidates,
        confidence=confidence,
        ambiguity=fields["Ambiguity"],
        training_captures=int(fields["Training captures"]),
        training_frames=int(fields["Training frames"]),
        inverted=fields["Polarity"] == "inverted",
        receive_only=True,
        review_required=True,
    )


def _compile_command(args: argparse.Namespace) -> int:
    captures = [parse_sub_capture(path) for path in args.capture]
    profile = compile_profile(args.name, captures)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(profile_to_text(profile), encoding="utf-8")
    print(
        f"profile={profile.profile_id} bits={profile.bit_count} "
        f"confidence={profile.confidence} ambiguity={profile.ambiguity}"
    )
    return 0


def _validate_command(args: argparse.Namespace) -> int:
    profile = _parse_profile_text(args.profile)
    if not profile.receive_only or not profile.review_required:
        raise ProtocolCompilerError("profile is missing receive-only review gates")
    for path in args.capture:
        result = decode_capture(profile, parse_sub_capture(path))
        if not result.stable_match or not result.checksum_match:
            raise ProtocolCompilerError(f"validation failed: {path}")
        print(f"{path}: 0x{result.value:X} ({result.bit_count} bits)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    compile_parser = subparsers.add_parser("compile", help="infer a receive-only profile")
    compile_parser.add_argument("--name", required=True)
    compile_parser.add_argument("--output", type=Path, required=True)
    compile_parser.add_argument("capture", nargs="+", type=Path)
    compile_parser.set_defaults(handler=_compile_command)

    validate_parser = subparsers.add_parser("validate", help="decode held-out captures")
    validate_parser.add_argument("--profile", type=Path, required=True)
    validate_parser.add_argument("capture", nargs="+", type=Path)
    validate_parser.set_defaults(handler=_validate_command)

    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, ProtocolCompilerError) as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
