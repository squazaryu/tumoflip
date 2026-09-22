# Tumoflip Dev 009-009 — release candidate

## Changes

- **Bluetooth Remote**: repair the host-stack controller path behind persistent
  `Cannot select peer / Retry from Devices`. Use supported ACI list operations,
  an advertising mode that actually enforces the selected-host filter, and
  controller privacy with the corresponding enhanced connection event. The
  default Companion profile remains on its original startup path. Existing bonds
  and local names are preserved; no pairing reset or radio-stack replacement.
- **Work Profiles**: named, SD-backed Standard/Read RAW settings, preset names,
  Protocol Pack, hopping and radio choice. Explicit session-scoped Apply; the two
  frequency/modulation pairs stay independent. UI is an on-demand FAL.
- **Decoder References** in Tumo Acceptance 0.3: user-confirmed RAW baselines,
  offline replay through existing decoders, source/result SHA-256, cancellation
  and result reports. No RF transmission or automatic baseline replacement.
- **Device Library 1.0**: notes, tags, manual checked-date and linked files.
  Card revisions use checked rotating records. File links never move originals;
  NFC opens its saved menu without automatically starting emulation.
- **File History**: explicit opt-in for controlled native SUB/IR/NFC saves;
  verified checkpoints before replacement, three recent versions and restore to
  a separate copy. Missing history engine, failed checkpoint or incompatible
  firmware cannot silently permit a protected write. Direct RPC and third-party
  writers are outside this serialized-editor contract. Copies live on the same SD.
- **Sensor Helper** in TumoSpectrum 3.1: fit bit-field interpretations to three
  measurements and check the fourth separately. An inspectable hypothesis list,
  correct measurement units and export; no automatic protocol installation or TX.

## Compatibility and delivery

- Public F7 API remains **88.13**. Private Sub-GHz feature ABI is **1002**:
  update Analyzer, Add Manually and Work Profiles FALs together with firmware.
- The BLE repair is in firmware, not `hid_ble.fap`. Bluetooth Remote 1.3 from
  Dev 019/020 remains usable and is retained byte-for-byte in Dev 021. A package
  reinstall alone cannot repair the old core. See [controller audit](ble-peer-controller-2026-09-22.md).
- Device Library + file_history.fal are delivered through **FW Packages Dev 021**,
  not updater resources. Native NFC/IR, Tumo Acceptance and TumoSpectrum come from
  the firmware updater. Their paths are not independent-catalog managed overlays
  in Dev 020, so that catalog does not overwrite them.
- Dev 021 must retain every existing Dev 020 package member byte-for-byte and add
  only the new Device Library/history pair. No catalog baseline/audit acceptance
  pin is advanced merely to prepare this release.
- Device Library uses `view_dispatcher_show_loading`, first exported by Tumoflip
  API 88.5; its standalone history FAL uses only API 88.0 exports. Automatic
  history additionally checks a live firmware capability including engine ABI.
- TumoCompanion 1.11.23 can parse the unchanged catalog schema and API-major range,
  but its bundled protection list lacks Device Library. A narrow companion update
  should protect the FAP and `/ext/apps_data/device_library/` family, and reject
  the new FAP on firmware missing its required loading-view export.

## Installation and acceptance

Install the updated companion first when available, then the complete firmware
updater including SD resources, then FW Packages Dev 021. Never overwrite an older
published release. Keep personal captures backed up off the SD before testing.

Physical acceptance is still pending, not covered by host tests:

1. Alternate profiles with different Standard/RAW pairs; Back from all pages;
   missing preset/module cases; repeat entry/exit and check heap stability.
2. Add a known RAW reference, confirm it, verify a match; change/remove/truncate
   a copy and verify an error rather than a false match; test cancellation/export.
3. Create/edit/reopen a device card; missing/long paths; verify opening NFC does
   not start emulation; check dirty-card discard/keep behavior.
4. Enable history, edit test SUB/IR/NFC files, restore a prior version as a copy;
   verify original files and user data remain untouched. Check slow SD, missing
   history module and old-firmware capability states using nonessential test files.
5. Use four short captures of your own sensor with measured values. Confirm a
   failed held-out hypothesis stays failed; a match is not proof of semantics.
6. Smoke-test Bluetooth/Companion, NFC, SD, native apps and FW Packages status.
7. Cold-start Bluetooth Remote with a phone and PC nearby. Select/name the PC;
   the phone must not take over while the PC is unavailable. Check explicit Add,
   cancel, reconnect after host address rotation, rename, repeat launch, and return
   to Companion. Test Forget only with a disposable bond. Record exact firmware,
   FW Packages and Remote versions if any action still fails.

No device installation, real-sensor acceptance or power-loss test is implied by
the native fixtures or by publishing a Dev candidate.
