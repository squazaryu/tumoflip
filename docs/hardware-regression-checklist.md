# Hardware Regression Checklist

Checklist version: 2

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
- checklist version: 2
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
- Sub-GHz capability descriptor and preflight tests in
  `tools/tumoflip/test_subghz_protocol_capabilities.py`
- safe acceptance-suite contract tests in
  `tools/tumoflip/test_hardware_acceptance_suite.py`

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
- Open `Apps` -> `Tools` -> `Tumoflip Packages` -> `Run Audit`; confirm the
  report opens, returns to the menu with Back, and shows no missing or
  mismatched package files on a clean install.
- Confirm no unexpected `FW packages` update warning remains after package
  installation.
- Open `Apps -> Module One -> Diagnostics -> Tumo Acceptance`, run `Run Safe
  Test`, and confirm private storage R/W is `PASS` while NFC and GPIO remain
  explicitly `SKIP` in this FAP-only phase.
- Confirm `.storage_probe.tmp` is absent after the safe test, then export a
  report and verify it contains schema `tumoflip.acceptance/1`, firmware/API
  identity, SD capacity, battery state, and the CC1101 broker baseline.
- Run export twice without deleting the first report; confirm an existing report
  is never overwritten and no unrelated SD file changes.

### System Sub-GHz Internal CC1101

- Open Desktop `Sub-GHz`; confirm it starts `ARF Sub-GHz Full`.
- Select `Standard Sub-GHz`; confirm it starts the core firmware app.
- Run `Read` with internal CC1101 and confirm RX starts.
- Run `Read RAW`, start and stop capture, then exit without reboot.
- With Standard hopping off, set Standard to 315 MHz/AM650. Open `Read RAW ->
  Config`, set 433.92 MHz/FM238, exit, and reopen both screens. Confirm each
  screen retains its own frequency and modulation.
- Change Standard to 868.35 MHz/AM270 and confirm the saved Read RAW profile is
  still 433.92 MHz/FM238. Then change Read RAW back to 315 MHz/AM650 and confirm
  Standard remains 868.35 MHz/AM270.
- Save a legal test signal and reopen it from Saved.
- With hopping off, select 315 MHz/AM and open the protocol list. Confirm
  `Linear` and `Cham_Code` are selectable; change to 433 MHz and confirm both
  are shown as incompatible instead of being silently accepted.
- Confirm `Holtek_HT12X` remains selectable on its declared 315/433/868 MHz
  bands with a matching AM/FM preset.
- Attempt to transmit a saved test file with a deliberately mismatched
  frequency or preset. Confirm TX is rejected with a concrete reason before
  the CC1101 enters transmit mode.
- Exit Sub-GHz and reopen it three times without a radio lock or crash.

### External CC1101 And Module One

- Connect Module One or another supported external CC1101 module.
- Set Infrared/Sub-GHz GPIO settings to the intended automatic mode.
- Confirm external CC1101 is detected when present.
- Repeat one compatible and one incompatible TX preflight with the external
  CC1101 selected; confirm the same fail-closed result as with the internal
  radio.
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
- Launch `Sub-GHz Remote`, start/stop a known safe saved remote, exit, and
  confirm core `Sub-GHz` can still receive afterward.
- Launch Apps -> Scripts -> JS Runner, then run `Scripts/js_examples/subghz.js`
  with transmit disabled or a known safe owned signal. Exit and confirm core
  `Sub-GHz` can still receive afterward.

### ARF Tools

- Open Desktop `ARF Tools`; confirm it starts `Module One Cockpit`.
- Open Apps -> ARF Tools.
- Launch `ARF Sub-GHz Full`; confirm it is only a launcher.
- Launch and exit `Frequency Analyzer`.
- Launch `Sub-GHz RAW Edit`, open a known `/ext/subghz/*.sub` RAW capture,
  exit without saving, and confirm no radio broker lock remains.
- Launch `KeeLoq Keystore Decryptor` only on your own test SD/firmware setup;
  confirm it exits cleanly and do not share `/ext/keystore_decrypted.txt`.
- Launch `Garage Door Remote`, enter/exit its receive views, and confirm core
  `Sub-GHz` can still receive afterward.
- In `Garage Door Remote`, start AM receive and confirm a known sample from the
  expanded protocol registry is recognized when suitable hardware is available.
- Launch `RollJam` from `ARF Sub-GHz Full`, back out without capture/transmit,
  and confirm core `Sub-GHz` can still receive afterward.
- Launch and exit every packaged ARF child FAP:
  `arf_keeloq`, `arf_counter_bf`, `arf_car_emulate`, `arf_psa_decrypt`,
  `proto_pirate`, `rolljam`, `subghz_bruteforcer`, and `arf_status`.
- Confirm a child FAP crash or exit does not leave the Radio Broker locked.
- Open `ARF Status -> RF Capabilities`; confirm schema version, RX/TX and AM/FM
  totals are present and the `Holtek_HT12X`, `Linear`, and `Cham_Code` rows
  match the Standard Sub-GHz compatibility view.
- In one ARF transmitter module, repeat the mismatched frequency/preset case
  and confirm it is blocked before TX.
- Reopen core `Sub-GHz` after ARF testing and confirm RX starts.
- With Module One connected, open `Apps -> Module One -> Sub-GHz -> TumoNet
  Bench` or `Desktop -> Cockpit -> CC1101: TumoNet`. Pair the RAM-only bench
  nodes, then run `Clean` in both `INT > EXT` and `EXT > INT` directions. Both
  runs must pass and show non-zero RF TX/RX counters.
- Run the `Drop`, `Duplicate`, `Replay`, `Corrupt`, `Wrong key`, and `Interrupt`
  scenarios. Each must reach `PASS` for its expected rejection/recovery result,
  without a crash or stuck external 5 V supply. Save one report and verify it
  contains counters but no key, plaintext, ciphertext, or authentication tag.
- Press `Stop` during an active TumoNet run, then leave with Back. Reopen core
  `Sub-GHz` and `ARF Sub-GHz Full` to confirm the dual-radio broker lease and
  both CC1101 devices were released.

### BLE App Bridge And Runtime

- Pair/connect with the iOS companion.
- Open `Settings -> Clock`, select the Time row, hold `OK`, and confirm the
  small `sync` indicator changes to `ok` and the Flipper time matches iPhone.
- Change the Flipper clock by at least one minute, open Authenticator, and
  confirm `TIME!` appears; resynchronize in Clock and confirm the warning is
  absent after reopening Authenticator.
- Confirm legacy FAB1-compatible traffic still works where applicable.
- Query FAB2 Runtime capabilities.
- Query FAB2 Runtime status and confirm firmware identity, API, transfer field,
  SD readiness, package-state presence, App Bridge session owner, and Radio
  Broker state are present.
- Send Runtime `hello`, confirm a v3 session ID is returned, then query
  `status` and confirm `sid` and `bo` are present.
- Query Runtime `trace` after several commands and confirm the compact
  `schema=1` ring includes recent `r` and `t`/`e` events without exceeding
  one response frame.
- Open `Apps -> Module One -> Diagnostics -> Runtime Trace`, confirm the
  Runtime Trace viewer renders the same compact ring, export a report, and
  verify `/ext/apps_data/runtime_trace_viewer/trace_*.txt` exists.
- Open `Apps -> Module One -> Field -> Field Logger`, start a session, press
  `OK` to add a manual sample, press `Right` to import the latest ARF
  Frequency Analyzer notebook observation or record the missing-source state,
  then press `Back` and confirm CSV/JSONL/GPX files exist under
  `/ext/apps_data/field_logger/sessions` and contain the iPhone fix when phone
  location sharing is enabled.
- Save one `.sub`, `.nfc`, and `.rfid` capture and confirm each primary file is
  unchanged and has a valid adjacent `<source>.tumoflip.json` sidecar. Disable
  location sharing and repeat one save; the capture must still succeed without
  creating or corrupting a sidecar.
- Complete one TumoSurvey session and save one TumoSpectrum report; confirm
  their committed output has an adjacent location sidecar when GPS is enabled.
- Query Runtime `twin` and confirm Device Twin fields reflect live firmware,
  SD/package, battery, Radio Broker, and App Bridge owner state.
- Send a small Runtime command and verify a response or explicit error frame.
- Open `Apps -> Module One -> Labs -> TumoFabric Node`. Start the local Counter,
  change it with `-1`/`+1`, reset it, and confirm Back returns without a crash.
- Keep Companion `Settings -> Diagnostics -> TumoFabric Counter` open, then
  start the local Counter on Flipper. Companion must attach automatically
  without a second Start tap. Increment once, retry the same sequence without a
  second increment, disconnect/reconnect BLE, resume the same value, then
  cancel. The on-device node must return to `IDLE` and normal BLE operation
  must remain available.
- Open TumoFabric Mac Node with USB connected. Its idle probe must not start a
  session. Start on Flipper and confirm Mac attaches automatically. Change the
  counter from Flipper, Mac, and iPhone; all three views must converge without
  changing the BLE owner. Unplug/replug USB and confirm Mac reconnects. Quit the
  Mac app and confirm qFlipper can immediately reopen the serial connection.
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
