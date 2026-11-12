# Fiat V2 save handling and Renault V1 reception

ProtoPirate 3.2.1 preserves the existing Tumoflip app and fixes its Fiat V2
format writes. Every Raw/Serial/Cnt/Hop/Btn write is checked. A malformed
capture is parsed into temporary values, so a failed open preserves the
previously loaded frame. This is a selective local correction following the
3.8 save-field change, not a wholesale source upgrade.

Standard and ARF Sub-GHz can load `protocol_renault_v1.fal` in the Europe pack.
RAW Auto Decode reaches the same pack through its existing scan sequence. The
plugin observes a 16-bit header and 88-bit payload, checks the XOR checksum,
and displays the observed serial, button, counter and authenticator fields.
Repeated identical frames are suppressed until a reset or changed capture.

The receive timing and raw field layout are adapted from ProtoPirate Renault
V1 as distributed in Community Pack `1oct2026p3`, exact source
`3e6a9ca199cdb4664c1998952deedc8533c420d5` (GPL-3.0). Local code checks frame
lengths and format-write failures, keeps the last good frame through invalid
input, and registers no encoder. It does not import the upstream key-recovery
or transmitter code. The existing 64-bit Renault implementation remains a
different protocol.

New files store the full 11-byte payload as Raw and its final 24 bits as an
eight-byte Key2. Opening also accepts the older Key_2 spelling and three-byte
tail format when Raw is absent; malformed size, header or checksum fails.
API 88.14 and the existing protocol-plugin ABI are unchanged.

## Device acceptance

Host tests exercise synthetic traces, noise, truncation, checksum/header
failures, duplicate/reset behavior, legacy file opening and injected format
write errors. Hardware reception is not inferred from these tests.

- On an owned Fiat V2 capture, save/open and compare Raw, Hop, Btn and counter
  before/after. Keep a separate copy of the original capture.
- In Standard → Config select Europe and confirm Renault V1 is listed by Pack
  Status. Decode an owned Renault V1 RAW capture, save it and reopen it; compare
  the full payload and displayed fields. Verify Auto Decode restores the
  previously selected Protocol Pack on exit.
- Smoke-test existing Renault, Fiat, Kia and the independent Standard/Read RAW
  frequency/modulation settings.

## Specter 3.3 acceptance on FW Packages Dev 023

These steps remain open until the owner reports results on the device:

- Run Verify on device and open Specter. Confirm About shows version 3.3.
- Confirm old sensitivity, alerts, logging and meter settings survived. Open
  existing logs and create one new log without losing the old records.
- Toggle Intro, reopen Specter, skip the splash and use Back from Sweep and
  Settings; the prior screen should be visible without stale overlays.
- Compare Field % and Duty % with the same owned NFC source. Detection and
  feedback should be consistent while the visible scale changes.
- Repeat opening/closing with TumoCompanion BLE connected. Record any error;
  device results are separate from the passed host/UI checks.
