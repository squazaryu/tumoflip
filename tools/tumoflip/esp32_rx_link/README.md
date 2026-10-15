# Tumo RX Link — experimental Module One backend 0.1.0

Target: Module One v6.1 with classic ESP32 and its external CC1101 connected to
Flipper. **Not ESP32-C5. Not a Marauder update. Hardware acceptance pending.**

This is an original receive-only diagnostic, not the upstream Pandora emulator.
It receives pulse timings over UART and returns bounded aggregate statistics.
There are no RF transmit, keyfob emulation, PIN/seed search, firmware-HEX execution,
network or SD-writing commands. Wi-Fi/BLE are not initialized.

## Installation boundary

Installing this image **replaces Marauder on the ESP32**. It does not change the
Flipper SD or install itself automatically. Keep a known-good Module One v6.1
Marauder factory package and its normal restoration instructions before testing.
Do not reuse the ESP32-C5 layout or label this archive as a Marauder factory pack.
Do not erase the entire ESP flash as part of the normal test. No flash operation
is performed merely by running the FAP.

The archive has an explicit `rx-link-build.json`, with chip, source commit,
offsets and hashes. It is intentionally NOT a TumoCompanion ESP32 package recipe.
Use an explicitly selected classic-ESP32 serial flashing tool for the three
listed images: bootloader at 0x1000, partition table at 0x8000, application at
0x10000. Never guess a device port or flash the connected board automatically.
The normal Marauder package is a separate restoration operation, not this image.

## Build

Pinned environment: ESP-IDF v5.3.2, target `esp32`, flash size 4 MiB. The matching
CI workflow records the exact source and packages only verified ESP32 images.

```sh
idf.py set-target esp32
cmake --build build --parallel 2
python3 package_backend.py --source-commit <full-source-commit>
```

UART0 is 230400 8N1, TX GPIO1 to Flipper RX pin 14 and RX GPIO3 from Flipper TX
pin 13, common ground. Runtime console logging is disabled so it cannot corrupt
the binary transport. Boot noise is discarded by the bounded frame parser.
Use the board's documented power and routing switches; do not wire two outputs
together. The Flipper FAP owns Module One power and CC1101 through its broker.

## Use and limits

Open `Module One / Signals / Tumo RX Link`, select one of 315/433.92/868.35 MHz
and AM650/FM238, then Start. The Flipper requires the matching board/version and
fresh-session acknowledgement before starting receive DMA. An ordinary Marauder
firmware times out without starting capture. Back always exits; OK stops/restarts.
Left/Right switches between histogram, timing details and loss diagnostics.

- Statistics are RAM-only; exiting discards them, with no capture files altered.
- Histogram bins are floor(log2(duration in microseconds)), with the last bin
  collecting durations >=32768 us. Min/max/mean and high-level time are separate.
- At most 1000000 pulses, each 1..1000000 us, are accumulated per session.
- UART bandwidth is finite. Buffer drops, sequence gaps, rejected batches and
  framing errors are visible; a lossy capture is NOT a complete signal recording.
- This reports timing statistics, not protocol identification, credentials or
  confidence that a receiver would accept a signal.
- The local session nonce prevents stale replies, not hostile-party authentication.
- No claim of improved core flash space: this is a FAP and external firmware;
  the existing firmware reserve/API policy remains unchanged.

## Acceptance on owned hardware

First test with synthetic/owned low-rate signals, then no signal, dense noise,
UART disconnection, Module One power loss, Stop/Back/restart and a second session.
Check that counters clear on restart and stale data never becomes LIVE. Check
that normal Sub-GHz can reacquire the radio after exit. Restore Marauder and test
its normal workflow separately. Successful CI is not hardware acceptance.
