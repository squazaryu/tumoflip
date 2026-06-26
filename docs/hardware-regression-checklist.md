# Hardware Regression Checklist

Checklist version: 1

Use this checklist for tumoflip release candidates before marking a release as
hardware-validated. CI can build firmware, validate packages, run unit tests,
and check updater safety. CI cannot validate RF behavior, external modules,
NFC tags, BLE pairing, sleep/wake behavior, heap pressure, or physical SD
package installation.

Do not mark a hardware-only item as passed unless it was executed on a physical
Flipper Zero for the exact firmware commit being released.

## Release Record

Copy this block into release notes after hardware testing. If the release is
published before hardware testing, keep the status as `not run` and list all
hardware cases as unverified.

```text
Hardware validation
- status: not run | partial | passed | failed
- checklist version: 1
- firmware version:
- release tag:
- firmware commit:
- device:
- SD card:
- external module:
- companion app version:
- operator:
- date:
- cases run:
- cases unverified:
- failures/issues:
```

## Automated Checks

These checks are allowed to be marked by CI:

- firmware build: `./fbt COMPACT=1 DEBUG=0 updater_package`
- release tests under `tools/tumoflip/test_*.py`
- updater, C2 gap, and package validation through
  `tools/tumoflip/validate_release.py --write-manifest`
- generated `tumoflip-packages.json`
- generated `tumoflip-packages.zip`
- release asset SHA-256 sums

## Hardware Matrix

Run hardware checks only with devices, modules, signals, and tags you own or
are legally allowed to test.

### Install And Identity

- Back up `/int` and important SD files before flashing.
- Install the release updater package from the GitHub Release.
- Reboot once after installation.
- Confirm firmware version, origin/fork, API version, and build cleanliness.
- Confirm SD card is detected after reboot.
- Confirm the companion can read `tumoflip-packages.json` and verify SD package
  hashes.
- Confirm no unexpected `FW packages` update warning remains after package
  installation.

### System Sub-GHz Internal CC1101

- Open Desktop `Sub-GHz`; confirm it starts the core firmware app.
- Run `Read` with internal CC1101 and confirm RX starts.
- Run `Read RAW`, start and stop capture, then exit without reboot.
- Save a legal test signal and reopen it from Saved.
- Exit Sub-GHz and reopen it three times without a radio lock or crash.

### External CC1101 And Module One

- Connect Module One or another supported external CC1101 module.
- Set Infrared/Sub-GHz GPIO settings to the intended automatic mode.
- Confirm external CC1101 is detected when present.
- Confirm fallback to internal CC1101 when the external module is absent or not
  acquired.
- Exit every radio app and confirm OTG/external power ownership is cleaned up.
- Reopen Sub-GHz after external-module use and confirm RX still starts.

### Protocol Packs

- Open Receiver Config and confirm `Protocol Pack` shows the saved group.
- Switch through `Core`, `Legacy`, `Kia`, `Ford`, `Europe`, `Asia/US`, and
  `Alarm`.
- After every switch, confirm RX resumes without restarting Sub-GHz.
- Open `Pack Status` and confirm loaded versus expected counts.
- Temporarily remove or corrupt one non-critical `.fal` on SD, confirm the UI
  reports the missing or invalid pack, then restore the file.
- Record the minimum free heap shown during pack switching or status display
  when available.

### Hopping

- Test Frequency hopping with adaptive dwell.
- Test Preset hopping.
- Test Combined hopping.
- Confirm a stronger legal test signal causes the hopper to hold long enough to
  attempt decoding.
- Confirm the hopper releases after the configured grace/max-hold windows.
- Stop hopping, leave Sub-GHz, reopen it, and confirm normal RX still works.

### ARF Tools

- Open Desktop `ARF Tools`.
- Launch `ARF Sub-GHz Full`; confirm it is only a launcher.
- Launch and exit `Frequency Analyzer`.
- Launch `Sub-GHz RAW Edit`, open a known `/ext/subghz/*.sub` RAW capture,
  exit without saving, and confirm no radio broker lock remains.
- Launch and exit every packaged ARF child FAP:
  `arf_keeloq`, `arf_counter_bf`, `arf_car_emulate`, `arf_psa_decrypt`,
  `proto_pirate`, `rolljam`, `subghz_bruteforcer`, and `arf_status`.
- Confirm a child FAP crash or exit does not leave the Radio Broker locked.
- Reopen core `Sub-GHz` after ARF testing and confirm RX starts.

### BLE App Bridge And Runtime

- Pair/connect with the iOS companion.
- Confirm legacy FAB1-compatible traffic still works where applicable.
- Query FAB2 Runtime capabilities.
- Send a small Runtime command and verify a response or explicit error frame.
- Disconnect and reconnect without rebooting the Flipper.

### NFC

- Read a MIFARE Ultralight/NTAG card with PWD/PACK data and confirm the read
  success screen displays the values.
- Read a Bambu Lab filament spool tag and confirm parser output.
- Exit NFC and reopen it without lag, crash, or stuck storage activity.

### Sleep/Wake And Repeated Launch

- Let the device sleep, wake it, and confirm Desktop still responds.
- Repeat App launch/exit cycles for Apps, Sub-GHz, ARF Tools, NFC, and Settings.
- Lock/unlock the device and confirm quick menu and Desktop OK menu still route
  correctly.
- Leave RX running for a long enough session to reveal heap/radio regressions,
  then exit cleanly.

## Failure Report

Every failed hardware case should include:

- firmware version, release tag, and commit
- checklist version
- exact app and menu path
- active radio module: internal CC1101 or external module model
- active Protocol Pack group, if relevant
- companion app version, if BLE or package install is involved
- reproduction steps
- expected result
- actual result
- heap/free-memory value, if visible
- whether reboot was required
- attached files, logs, photos, or videos when useful
