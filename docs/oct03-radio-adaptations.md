# October 3 candidate — passive analysis and packet infrastructure

Source candidate 009-018; not a published release or hardware acceptance.

- Casi-Rusco: selectively adapt Unleashed #1163 head 5163d948. Exhaustive tests
  compare raw fields with the independent reader rule, not only local round trips.
- Prastel: adapt RX serial layout and first-frame gap from 7c1b4fff. No manual
  generation/button TX changes from e4eda9ef are imported.
- Official #4458 concept: copied, length-bounded custom CRC/variable-packet
  presets, channel frequency validation, bounded calibration failure and rollback.
  No hard-coded alternative asynchronous presets are treated as packet-compatible.
  Worker startup reports radio/lease errors synchronously; normal ownership stays
  on its worker thread. CLI Chat explicitly borrows its already-acquired lease.
  Cleanup does not release the caller's lease or close its record.
- FIFO reads reserve all 64 bytes the driver may return; oversized payloads are
  discarded. No TX attempt starts after a rejected base/channel frequency.
- Marauder adds passive Remote ID scan/list and strictly validated target index.
  Enable console logging to save `remoteid*` logs. Board UART schema is pinned to
  ESP32Marauder 9afe7d117; stable 1.17.0 has no Remote ID. No board is auto-flashed.

## Compatibility

Local F7 API is 88.15, not upstream API parity. Checked/copying interfaces use
different names from incompatible upstream void/borrowed-pointer signatures.
Radio-device plugin ABI **1003** is deliberately distinct from upstream 3 and
our old 2. Install the paired external driver; foreign/stale descriptors are
rejected. FW Packages need a full paired rebuild for this candidate.

No user captures, dictionaries or settings are migrated/deleted by these changes.
Existing dual-use apps are not expanded; automotive brute-force additions and
deferred protected-audit dispositions remain untouched.

## Hardware checks still required

- Real Casi cards/reader, including boundary cases; owned Prastel RAW captures,
  first-frame reception and noise rejection.
- Existing CLI Chat on internal and external CC1101: start, send/receive on an
  authorized lab setup, Ctrl+C, module disconnect, restart and broker restoration.
- Variable-packet custom preset/channel 0/nonzero, invalid configuration and
  repeated startup/teardown; verify the actual RF frequency with lab equipment.
- Remote ID on compatible ESP32 firmware: own drone, UART/log capture and stop;
  older/unsupported board must fail explicitly, not fabricate observations.

Host tests/builds/import validation do not mark these physical checks passed.
