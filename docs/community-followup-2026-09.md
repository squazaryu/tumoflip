# Community review follow-up — unreleased

Scope approved by the user: Quac durations/cancellation, embedded ABI checking,
Specter, and receive profiles. **Do not update SubBrute/Agentic Remote protected
audit decisions or mark unrelated reviews/hardware acceptance complete.**

## Quac 0.11.0

- NFC/RFID/iButton and timed Sub-GHz actions accept 100..60000 ms. Invalid saved
  values keep defaults; invalid playlist overrides stop the playlist with an
  error. Parsing rejects signs, extra tokens and overflow.
- Playlist pauses accept 0..60000 ms, including sub-100 ms pauses.
- Event-based waits replace narrow signed/unsigned countdown loops. A 150 ms
  duration remains 150 ms; 60000 ms does not become negative.
- Back cancels waits and subsequent playlist actions. RAW waits accept completion
  or Back, and the wait context is freed only after the radio worker stops.
- Existing cleanup, radio broker and Picopass safeguards remain. A single IR
  send is still an atomic action; this is not a redesign of every Quac action.

## Specter 3.1.0-tumo

The MIT-licensed upstream 3.0 source is adapted as a **package-only FAP**, not a
resident firmware module. See `applications_user/specter/PROVENANCE.md`.

Both TXT and CSV writes must succeed before success is reported. Append capacity
includes the whole new record/header; short write/sync failures trigger a
best-effort rollback. Close failures remain errors. Log-full feedback is retained
even if the last complete record ends below the byte limit. A CSV failure can
leave a complete TXT entry: there is no claim of an atomic two-file transaction.

Only external 13.56 MHz field presence is measured. No new RF transmission or
inference that a detected reader is malicious. Physical field loss, calibration,
SD removal and UI behavior still require hardware checks.

Paired package target: `/ext/apps/NFC/specter.fap`. It is built/exported locally
with the firmware API. The independent catalog and protected registry have not
been published/changed. Before distributing a maintained replacement, explicitly
resolve ownership against the Community Pack `specter` entry so subsequent
Community installs cannot silently overwrite the adapted binary. This is separate
from, and must not alter, the excluded SubBrute/Agentic Remote audit decisions.

## Receive profiles

`RX profile` is available in Standard Sub-GHz and Read RAW Config, plus the
isolated ProtoPirate receiver Config. It offers Manual and seven fixed choices
adapted from the reviewed ProtoPirate model data: four 315/433.92 AM/FM pairs,
two named Ford aliases and the example custom Honda preset.

These are **frequency/modulation presets**, not car identification or confirmed
vehicle compatibility. No encoder, brute-force or secret-recovery behavior is
added. Manual leaves the current pair in place; changing frequency/modulation
clears the profile label. Standard and RAW retain separate saved settings.

Hopping must be off for a Standard/ProtoPirate profile change. An unavailable
preset is reported rather than indexed blindly. The known custom preset is owned
by the existing settings object; a user preset with a conflicting name is never
overwritten. Custom-preset registration publishes its allocation only after the
complete data is read. There is no external Models DB or saved model index loaded
at startup, and no new field shifts the ProtoPirate plugin context layout.

This first adaptation is a fixed catalog, not an editor for arbitrary model
database files. Existing manual custom-preset configuration remains available.

## Deterministic ABI audit

Implemented separately in `tumoflip-community-abi-embedded` on
`feat/community-abi-embedded-hosts`. It checks bounded ELF manifests, recursively
reads `.fapassets`, validates internal checksums and binds FAL imports to their own
content-pinned host API. Another FAP's exports cannot mask a missing dependency.
No scheduled LLM prompt, schedule, protected disposition or published audit ledger
was changed. See that repository's `docs/community-plugin-abi.md`.

## Physical acceptance still needed

- [ ] Quac: 150/60000 ms from a controlled playlist; invalid 0/overflow/extra-token
  action values; Back during NFC/RFID/iButton/timed Sub-GHz/RAW and playlist pause.
  Confirm the worker/radio stops and the next playlist item does not execute.
- [ ] Specter: known reader present/absent, repeated calibration, busy NFC, all
  four views, log full and SD removal; TXT/CSV must never be falsely acknowledged.
- [ ] Profiles: seven choices, Manual, hopping guard, internal/external radios;
  different Standard/RAW pairs survive reopening. ProtoPirate exits/reopens Config
  safely and restores its receive path after a module/SD failure.
- [ ] Firmware smoke: NFC, RFID, SD, BLE/TumoCompanion, existing FAPs and packs.

No release, device installation or independent catalog publication is part of
this implementation turn. Test artifacts keep the old distribution suffix only
for controlled size comparison; never replace published 009-002 with them.

## Validation and footprint

Clean firmware/FAP source build: `095d0ef2e08d3c135bffebc940a08f139c8bffaf`
(`BUILD_DIRTY=0`, API 88.10, Flipper 39/GCC 12.3, COMPACT=1, DEBUG=0, LTO=1,
`-j2`). The subsequent report/test-only commit does not change the compiled code.

- 389 release-workflow tests passed. Native tests execute the full Quac wait
  adapter lifecycle and production Standard profile callback, as well as the
  duration/catalog/logger helpers; no physical radio transmission is performed.
- F7/updater/SDK and every requested package target, including `fap_specter`,
  built successfully; APPCHK passed. Existing duplicate-environment/serial-LTO
  warnings remain, rather than being suppressed.
- Updater validated with 8,192 B page-aligned C2 gap and 9,896 B section gap.
- All 116 paired-package files match their manifest size/SHA-256/MD5.
- Control-plane branch: 277 tests pass. The exact Community Pack scan reaches
  418 binaries and leaves the three known GPS-stream findings unresolved.
- Protected decision/registry files and the protected-audit workflow are byte
  unchanged against the control-plane starting commit.

| Metric | Before this follow-up | After | Difference |
|---|---:|---:|---:|
| Section gap before C2 | 10,800 B | 9,896 B | -904 B |
| Page-aligned C2 gap | 8,192 B | 8,192 B | 0 B |
| Core `.bss` | 7,564 B | 7,564 B | 0 B |
| Quac FAP on SD | — | 81,160 B | Separate from core |
| Specter FAP on SD | absent | 59,940 B | Separate from core |

Flash address-range growth is 904 B for this follow-up, 2,112 B versus published
009-002. Alignment is included; summing section sizes alone can differ by padding.
RAM peak and real-device response times were not measured. The physical-gap value
is calculated from the build images and radio address, not read from a device.
Digests and exact metrics: `.ecc/benchmarks/community-followup-2026-09-19.json`.
