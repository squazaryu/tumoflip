#!/usr/bin/env python3
"""Hardware smoke test for the TumoVM USB CCID transport on macOS."""

from __future__ import annotations

import argparse
import ctypes
import sys
from dataclasses import dataclass
from pathlib import Path


PCSC_FRAMEWORK = Path("/System/Library/Frameworks/PCSC.framework/PCSC")
SCARD_SCOPE_SYSTEM = 2
SCARD_SHARE_SHARED = 2
SCARD_PROTOCOL_T0 = 1
SCARD_LEAVE_CARD = 0
SCARD_S_SUCCESS = 0
SCARD_E_NO_READERS_AVAILABLE = 0x8010002E

SELECT_APDU = bytes.fromhex("00 A4 04 00 05 F0 54 56 4D 01")
READ_APDU = bytes.fromhex("00 B0 00 00 04")
SMOKE_MARKER = bytes.fromhex("54 56 4D 21")


class PcscError(RuntimeError):
    pass


class ScardIoRequest(ctypes.Structure):
    _fields_ = [("dwProtocol", ctypes.c_uint32), ("cbPciLength", ctypes.c_uint32)]


@dataclass(frozen=True)
class ApduResponse:
    data: bytes
    status: int


class Pcsc:
    def __init__(self) -> None:
        if sys.platform != "darwin":
            raise PcscError("This smoke test requires macOS PCSC.framework")
        self.lib = ctypes.CDLL(str(PCSC_FRAMEWORK))
        self.context = ctypes.c_int32()
        self.card = ctypes.c_int32()
        self._bind()

    def _bind(self) -> None:
        self.lib.SCardEstablishContext.argtypes = [
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_int32),
        ]
        self.lib.SCardListReaders.argtypes = [
            ctypes.c_int32,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.SCardConnect.argtypes = [
            ctypes.c_int32,
            ctypes.c_char_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.SCardTransmit.argtypes = [
            ctypes.c_int32,
            ctypes.POINTER(ScardIoRequest),
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.SCardDisconnect.argtypes = [ctypes.c_int32, ctypes.c_uint32]
        self.lib.SCardReleaseContext.argtypes = [ctypes.c_int32]

    @staticmethod
    def _check(result: int, operation: str) -> None:
        if result != SCARD_S_SUCCESS:
            raise PcscError(f"{operation} failed: 0x{result & 0xFFFFFFFF:08X}")

    def open(self) -> None:
        self._check(
            self.lib.SCardEstablishContext(
                SCARD_SCOPE_SYSTEM, None, None, ctypes.byref(self.context)
            ),
            "SCardEstablishContext",
        )

    def readers(self) -> list[str]:
        length = ctypes.c_uint32(0)
        result = self.lib.SCardListReaders(self.context, None, None, ctypes.byref(length))
        if result & 0xFFFFFFFF == SCARD_E_NO_READERS_AVAILABLE:
            return []
        self._check(result, "SCardListReaders(size)")
        if length.value == 0:
            return []
        buffer = ctypes.create_string_buffer(length.value)
        self._check(
            self.lib.SCardListReaders(self.context, None, buffer, ctypes.byref(length)),
            "SCardListReaders(data)",
        )
        return [entry.decode() for entry in buffer.raw.split(b"\0") if entry]

    def connect(self, reader: str) -> None:
        protocol = ctypes.c_uint32()
        self._check(
            self.lib.SCardConnect(
                self.context,
                reader.encode(),
                SCARD_SHARE_SHARED,
                SCARD_PROTOCOL_T0,
                ctypes.byref(self.card),
                ctypes.byref(protocol),
            ),
            "SCardConnect",
        )
        if protocol.value != SCARD_PROTOCOL_T0:
            raise PcscError(f"Unexpected active protocol: {protocol.value}")

    def transmit(self, apdu: bytes) -> ApduResponse:
        send = (ctypes.c_uint8 * len(apdu)).from_buffer_copy(apdu)
        receive = (ctypes.c_uint8 * 258)()
        receive_size = ctypes.c_uint32(len(receive))
        pci = ScardIoRequest(SCARD_PROTOCOL_T0, ctypes.sizeof(ScardIoRequest))
        self._check(
            self.lib.SCardTransmit(
                self.card,
                ctypes.byref(pci),
                send,
                len(apdu),
                None,
                receive,
                ctypes.byref(receive_size),
            ),
            "SCardTransmit",
        )
        raw = bytes(receive[: receive_size.value])
        if len(raw) < 2:
            raise PcscError(f"Short APDU response: {raw.hex(' ')}")
        return ApduResponse(raw[:-2], int.from_bytes(raw[-2:], "big"))

    def close(self) -> None:
        if self.card.value:
            self.lib.SCardDisconnect(self.card, SCARD_LEAVE_CARD)
            self.card = ctypes.c_int32()
        if self.context.value:
            self.lib.SCardReleaseContext(self.context)
            self.context = ctypes.c_int32()


def require_ok(response: ApduResponse, operation: str) -> bytes:
    if response.status != 0x9000:
        raise PcscError(f"{operation} returned SW={response.status:04X}")
    return response.data


def choose_reader(readers: list[str], query: str | None) -> str:
    candidates = readers
    if query:
        candidates = [reader for reader in readers if query.lower() in reader.lower()]
    if not candidates:
        raise PcscError("No matching CCID reader found")
    if len(candidates) > 1:
        raise PcscError("Multiple readers match; pass --reader with a unique substring")
    return candidates[0]


def run_smoke(pcsc: Pcsc, reader: str) -> None:
    pcsc.connect(reader)
    require_ok(pcsc.transmit(SELECT_APDU), "SELECT")
    original = require_ok(pcsc.transmit(READ_APDU), "READ original")
    if len(original) != len(SMOKE_MARKER):
        raise PcscError(f"Expected four state bytes, received {len(original)}")

    update = bytes((0x00, 0xD6, 0x00, 0x00, len(SMOKE_MARKER))) + SMOKE_MARKER
    restore = bytes((0x00, 0xD6, 0x00, 0x00, len(original))) + original
    try:
        require_ok(pcsc.transmit(update), "UPDATE marker")
        observed = require_ok(pcsc.transmit(READ_APDU), "READ marker")
        if observed != SMOKE_MARKER:
            raise PcscError(
                f"State mismatch: expected {SMOKE_MARKER.hex(' ')}, got {observed.hex(' ')}"
            )
    finally:
        require_ok(pcsc.transmit(restore), "RESTORE original")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reader", help="Unique substring of the reader name")
    parser.add_argument("--list", action="store_true", help="List readers and exit")
    args = parser.parse_args()

    pcsc: Pcsc | None = None
    try:
        pcsc = Pcsc()
        pcsc.open()
        readers = pcsc.readers()
        if args.list:
            print("\n".join(readers) if readers else "No PC/SC readers found")
            return 0
        reader = choose_reader(readers, args.reader)
        print(f"Reader: {reader}")
        run_smoke(pcsc, reader)
        print("PASS: SELECT/READ/UPDATE/RESTORE")
        return 0
    except PcscError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    finally:
        if pcsc is not None:
            pcsc.close()


if __name__ == "__main__":
    raise SystemExit(main())
