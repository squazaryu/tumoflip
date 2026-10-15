# Tumo RX Link (experimental, Module One only)

This is an original passive timing-analysis FAP and paired ESP32 backend. The
ARF ESP32 frontend motivated the transport/offload approach, but its Pandora
firmware execution, keyfob emulation, PIN search and TX commands are not imported.

## User boundary

- Flipper path: `Apps / Module One / Signals / Tumo RX Link`.
- Target board: Module One v6.1, classic ESP32 plus external CC1101. Not C5.
- The backend image replaces Marauder only when explicitly flashed by the user.
  Opening the FAP does not flash anything; an unrecognized backend never starts RX.
- Existing captures, keystores and SD files are not read, replaced or deleted.
- This is not a protocol/credential decoder. It displays timing histograms,
  min/mean/max duration, high-level fraction and transport-loss diagnostics.
- Session results are RAM-only. The user must stop or exit before changing the
  radio configuration; restarting creates a fresh nonce and clears statistics.

## Architecture and validation

The Flipper acquires its UART and broker-owned Module One radio/power lease,
then performs a version/board/session handshake. Only a valid matching reply
allows receive DMA. Pulses pass through a bounded 512-entry ISR queue. UART
frames have bounded length and CRC16; the ESP32 validates whole batches before
updating its statistics. Counter arithmetic is bounded to one million samples.

The ESP32 cannot set frequency/registers or start RF transmission. Three local
frequency choices (315/433.92/868.35 MHz) and two presets (AM650/FM238) are checked
through the existing device API. No public firmware API or resident service is added.

Lost samples, missing batches and invalid replies are not silently presented as
a complete capture. A timed-out link or disconnected radio stops DMA and releases
UART/device resources. Stop/Back teardown stops interrupt producers before their
queues are freed. The local session nonce rejects stale replies; it is not a
cryptographic authentication scheme for an untrusted attached board.

Host tests compile the actual shared C code with ASan/UBSan and exercise the
production frontend poll/teardown functions against fake IO. Native previews
use production draw functions and firmware fonts, with text bounds checked on
all 15 illustrated states including errors and maximal diagnostic counters.

```sh
toolchain/current/bin/python3 -m unittest tools.tumoflip.test_rx_link
toolchain/current/bin/python3 -m tools.tumoflip.render_rx_link /tmp/rx-link-preview
FBT_NO_SYNC=1 ./fbt COMPACT=1 DEBUG=0 -j2 fap_tumo_rx_link
```

The shared parser/session/statistics implementation measured 99.5% line and
85.12% branch coverage. Those figures do not describe hardware acceptance or
the entire firmware. The first candidate retains a full physical 4096-byte
flash reserve, to be rechecked on the exact release commit.

## ESP32 build and installation

See [the backend instructions](../tools/tumoflip/esp32_rx_link/README.md).
The CI image is pinned by digest, uses ESP-IDF 5.3.2 and builds with two jobs.
The ZIP records its exact source SHA and per-image offsets/hashes. It is not
an automatic Companion/Marauder package and is not suitable for ESP32-C5.

Before testing, retain a backup of the ESP32 and a known-good Module One Marauder
restore package. Hardware checks on the owner's board remain pending: normal
receive, no signal, dense input/loss reporting, UART/power loss, Stop/Back,
reconnection, radio reacquisition and restoration of Marauder.
