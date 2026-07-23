<div align="center">

# tumoflip

**Custom Flipper Zero firmware** based on [DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware).

![base](https://img.shields.io/badge/base-Unleashed-black)
![target](https://img.shields.io/badge/target-Flipper%20Zero%20(f7)-orange)
![license](https://img.shields.io/badge/license-GPLv3-blue)
[![release](https://img.shields.io/github/v/release/squazaryu/tumoflip?label=release&color=brightgreen)](https://github.com/squazaryu/tumoflip/releases/latest)

📱 **Companion app:** [squazaryu/TumoCompanion](https://github.com/squazaryu/TumoCompanion) — native iOS (Feather / AltStore)

</div>

This repository keeps the Unleashed firmware history and adds a small set of
personal firmware changes on top of it. It is not an official Unleashed release
and is not affiliated with Flipper Devices or the Unleashed team.

This firmware contains experimental features and local changes. Some of them
may be unstable, incomplete, or incompatible with future Unleashed updates. If
you find a tumoflip-specific issue, report it in this repository:
[squazaryu/tumoflip issues](https://github.com/squazaryu/tumoflip/issues).

## Current Build

- Base: Unleashed 089 with selected upstream dev updates
- Firmware version: `t-dev-089-041-002`
- Firmware origin/fork: `tumoflip`
- Firmware API: `88.0`
- Target: Flipper Zero F7
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-089-041-002.tgz`
- Flash profile: JS Runner / MJS runtime excluded to preserve internal flash headroom.

API `88.0` is a deliberate breaking migration. External FAP/FAL binaries built
for API `87.x` must be rebuilt or replaced with the versions shipped in the
matching Tumoflip FW Packages release.

## Version Scheme

Stable `main` firmware versions use this format:

```text
t-flppr-fw-<unleashed>-<build>
```

Development `dev` firmware versions use this format:

```text
t-dev-<unleashed>-<build>-<iteration>
```

- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix.
- `tmwhflpprarf`: legacy stable prefix kept for existing releases.
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `089`: upstream Unleashed base version.
- `041`: tumoflip internal build version.
- `002`: development iteration inside the tumoflip internal build version.

`main` should only receive builds that are stable enough to publish as tagged
releases. Active firmware work lands on `dev` first. When the Unleashed base
version, tumoflip internal version, or dev iteration changes, update the
firmware version suffix in `fbt_options.py`, release notes, README, and the
published update package name together.

Companion and release tooling continue to recognize the legacy
`tmwhflpprarf<unleashed>-<build>` format. Existing release tags and packages
are never renamed; only new stable builds use `t-flppr-fw-*`.
Stable Git tags remain SemVer: stable `089-039` uses `v0.3.9` and the stable
firmware prefix. The current development line reports `t-dev-089-041-002`
through `device_info`.

For `dev`, every separate issue-level or user-visible firmware change should
advance the last three-digit iteration. A new Tumoflip internal build starts at
`001`; the next change in the same internal build advances that suffix to
`002`, then `003`, and so on. Use the helper to keep `DIST_SUFFIX`, README, and
the update splash synchronized:

```sh
python3 tools/tumoflip/bump_dev_version.py --build 036 --iteration 001
python3 tools/tumoflip/bump_dev_version.py
```

Prepare a new stable identity only on an approved release branch. This keeps
the current Unleashed/build numbers, removes the dev iteration, updates README
and splash assets together, and never rewrites an existing legacy release:

```sh
python3 tools/tumoflip/prepare_stable_version.py --dry-run
python3 tools/tumoflip/prepare_stable_version.py
```

The four-page post-update splash screen is generated automatically from
`DIST_SUFFIX` when `updater_package` is built.

## tumoflip Changes

- Rebranded firmware origin to `tumoflip` and distribution/version suffix to
  `t-dev-089-041-002`.
- Added custom Desktop main menu styles inspired by Momentum-style layouts.
- Keeps a bounded compressed Desktop animation preview while external FAPs exit,
  reducing the visible SD reload pause without retaining the full wallpaper in RAM.
- Added `RX Mode: AUTO/DUAL` and a direct Read-screen toggle to the system
  Sub-GHz app. With an external CC1101 connected, both radios decode
  independently and duplicate frames are merged while retaining the stronger
  source and RSSI.
- Added receive-only Preset Scan to ARF Frequency Analyzer. It ranks the
  available modulation presets at the selected frequency and can open the
  system Sub-GHz Receiver with the chosen preset.
- Removed repeated `MHz` labels from empty Frequency Analyzer history slots and
  the overlapping feedback glyph from the right column.
- Added autonomous receive-only protocol profiles: TumoSpectrum can infer a
  bounded `.tproto` profile from three or four Sub-GHz RAW captures directly on
  Flipper, save it atomically to SD, and start live decoding without a computer.
- Added TumoSpectrum Band Map with a bounded spectrum/waterfall, noise-floor and
  peak history, Internal/External CC1101 selection, and a stock-compatible Smart
  Capture path from a selected carrier to an on-device `.tproto` profile.
- Added `TumoTag Verify` under `Module One/NFC`: it compares saved NFC, LF RFID,
  and iButton artifacts with physical tokens, distinguishes verified, partial,
  different, and unsupported reads, finds matching saved files recursively, and
  stores bounded read-only reports without modifying tokens or source artifacts.
- Added `8/1` Module One folder after Apps in the Desktop OK menu.
- Replaced the Desktop OK menu `Sub-GHz Remote` shortcut with the `ARF Tools`
  folder while keeping the normal `Sub-GHz` entry on the standard core app.
- Preserves the original Desktop OK menu launch stack so returning from Apps,
  core apps, and folders goes back to the Desktop layout instead of the Dolphin
  home screen.
- Extends Desktop favorite shortcuts to launch selected `.fap` apps, `.js`
  scripts, the `8/1` Module One folder, and the `ARF Tools` folder.
- Added Module One icon based on the Rotten Mechanism cross mark.
- Added optional manifest-validated SD asset-pack overrides for custom Desktop
  OK-menu icons.
- Removed Dummy Mode and related shortcuts.
- Added Settings entry to the Desktop short-Up quick menu.
- Added an optional Desktop setting to skip the lockscreen door animation.
- Added BLE App Bridge support.
- Added BLE GATT Service Changed handling so bonded iOS clients can refresh
  stale App Bridge handles after firmware updates or BLE profile rebuilds.
- Added a Tumoflip Runtime transfer-activity command that shows a small BLE
  statusbar activity indicator while the iOS companion transfers plugins,
  firmware packages, or ESP32 firmware files.
- Added `FlipperRelay` host bridge tooling for Mac-side BLE automations.
- Added an initial ARF Sub-GHz protocol layer from
  [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF).
- Added the first TumoVM proof of concept under `Module One/Labs`: a bounded
  SD-loaded APDU state machine exposes one persistent object over NFC
  ISO14443-4A and app-local USB CCID without restoring the removed API 87 CCID
  HAL.
- Added `NFC CCID Bridge` under `Module One/NFC` for authorized ISO14443-4A
  development cards. It relays bounded APDUs to macOS PC/SC, starts read-only,
  never persists payloads, and restores NFC and USB ownership on exit.
- Added `TumoCard OS` under `Module One/NFC`: a bounded TumoVM registry loads up
  to four isolated SD applets, routes NFC/USB APDUs by unique AID, keeps separate
  persistent state, and supports on-device enable/disable controls.
- Added `TumoModule Runtime` under `Module One/Modules`: API 88 `.tmod`
  manifests select reviewed, resource-bounded adapters for Module One BME280
  temperature sampling and TumoVGM IMU diagnostics without loading native code.
- Added ARF Sub-GHz `setting_user` frequencies, hopper frequencies, and custom
  presets as ProtoPirate-only assets, isolated from the normal Sub-GHz app.
- Added ProtoPirate and ARF Tools as external apps for isolated SD deployment.
- Rebuilt ARF Sub-GHz Full as a lightweight launcher for the stable core
  Sub-GHz app and separate ARF FAPs. Each ARF child receives the full
  application heap instead of being linked into one large process.
- Exposes ARF Sub-GHz Full, ARF Frequency Analyzer, Sub-GHz RAW Edit, KeeLoq
  Keystore Decryptor, and Garage Door Remote in `/ext/apps/ARF Tools`; the
  remaining isolated child FAPs, including RollJam, are packaged under
  `/ext/apps_data/arf_subghz_full/modules`.
- Keeps the Desktop `Sub-GHz` shortcut on the stable core app; ARF Tools stays
  as a separate Desktop folder/launcher.
- Added MIFARE Ultralight/NTAG PWD and PACK to the NFC read-success screen.
- Added the Bambu Lab filament spool NFC parser.
- Added adaptive dwell and signal hold to hopping in the system Sub-GHz app.
- Added external Sub-GHz Protocol Packs so selected decoders can be loaded from
  SD without keeping a second copy in the core firmware image.
- Added the decode-only `Shuka Auto` Protocol Pack group from
  [shuka0158/ARF-Shuka-Edition](https://github.com/shuka0158/ARF-Shuka-Edition):
  GM Rolling, Honda/Acura, Hyundai New, Nissan, Renault, and Toyota/Lexus.
- Added runtime Protocol Pack switching: the receiver safely reloads the
  selected decoders and resumes active reception without restarting Sub-GHz.
- Added Protocol Pack Inspector to show the active group, loaded decoder count,
  FW/plugin API, pack RAM cost, and per-`.fal` load errors.
- Added a Sub-GHz Radio Broker for exclusive radio ownership, external CC1101
  power ownership, and internal-device fallback in system Sub-GHz and all
  current ARF radio applications.
- Added a release validator and versioned package manifest for reproducible SD
  app layouts and independent C2/updater safety checks.
- Added a hardware regression checklist and release-note warning for checks
  that require a physical Flipper Zero.
- Added a verified `tumoflip-packages.zip` release artifact for transactional SD
  package installation from the iOS companion app.
- Added package-only FW Packages releases for external `.fap`, `.fal`, and
  `/ext` resource updates that do not require a firmware version bump.
- Bundled the Module One `ESP32 Marauder` FAP into the SD package so it is
  available from the `8/1` Module One folder after a clean install.
- Added `WiFi Mapper`, a passive Module One ESP32 UART logger that starts
  Marauder AP scanning and stores scan output under
  `/ext/apps_data/wifi_mapper/sessions`; current packages include the GPS
  no-fix handling, GeoJSON export fixes, and an opt-in Companion live BLE relay.
- Ported safe all-the-plugins fixes into local apps: `Sub-GHz RAW Edit` 1.6
  raises its stack to avoid firmware-specific MPU faults, and `Quac` now guards
  OTG power changes and recognizes the `FM12K` Sub-GHz preset label.
- Vendored local user applications into `applications_user` so the repository
  can be built without absolute local symlinks.

## Differences from Unleashed

tumoflip is based on Unleashed, but it intentionally changes the parts of the
firmware that affect the Desktop, quick access flow, bundled apps, and build
identity.

| Area | Unleashed | tumoflip |
| --- | --- | --- |
| Firmware identity | Reports itself as Unleashed. | Reports `firmware_version: t-dev-089-041-002` and `firmware_origin_fork: tumoflip`. |
| Desktop layouts | Uses the default Unleashed Desktop style set. | Adds custom main menu styles, including Wii, DSi, Vertical, and Wii Vertical variants. |
| Dummy Mode | Included and reachable from Desktop shortcuts. | Removed from firmware and removed from shortcuts. |
| Short-Up quick menu | Includes the standard quick actions, including Dummy Mode in the original layout. | Replaces the removed Dummy Mode shortcut with Settings. |
| Desktop OK menu | Uses the standard app/menu layout. | Keeps the `8/1` Module One folder after Apps, keeps `Sub-GHz` on the core app, and replaces `Sub-GHz Remote` with the `ARF Tools` folder. |
| Desktop favorites | Can launch built-in apps or selected `.fap` apps. | Also supports `.js` scripts and direct folder targets for `8/1` Module One and `ARF Tools`. |
| ARF tools access | Apps are reached through the normal Apps tree. | Exposes one Full launcher; child ARF/ProtoPirate FAPs are internal modules under `apps_data`. |
| Settings return flow | Standard Unleashed navigation. | Keeps the Desktop Settings shortcut separate from the normal OK menu flow where possible. |
| BLE services | Standard Unleashed BLE behavior. | Adds BLE App Bridge support and Service Changed indications for stale iOS GATT cache recovery after firmware updates/profile rebuilds. |
| ARF protocols | Not included. | Keeps the core set size-limited and loads selected automotive decoders from SD as Protocol Packs. |
| Sub-GHz hopping | Frequency hopping only. | Adds preset and combined hopping plus an adaptive scan dwell, signal hold, post-signal grace period, and bounded hold time to system Sub-GHz. |
| NFC additions | Uses the Unleashed 089 NFC feature set. | Shows captured MIFARE Ultralight/NTAG PWD and PACK and adds the Bambu Lab filament spool parser. |
| User apps | External/local apps are not part of the base repository. | Vendors selected local apps into `applications_user` so the firmware builds reproducibly. |
| Build metadata | Uses upstream build metadata conventions. | Uses `t-dev-089-041-002` for the installed firmware version and release artifact suffix, while keeping `tumoflip` as the fork origin. |

## Notes on Custom UI

The custom Desktop styles are focused on changing the main Desktop launcher
experience, not replacing every nested app list in the firmware. Apps and
system screens still mostly follow the underlying Unleashed UI behavior unless
they were changed explicitly.

Custom `8/1` and `ARF Tools` OK-menu icons can be overridden from SD without a
firmware rebuild. See [Tumoflip Asset Packs](docs/tumoflip-asset-packs.md) for
the supported path, file format, generator script, and fallback rules.

Current custom Desktop modes:

- Wii
- Wii Vertical
- DSi
- Vertical

## ARF Sub-GHz Layer

tumoflip includes an initial merge of selected automotive Sub-GHz protocol code
from [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF).
Additional Shuka Auto protocol packs are credited to
[shuka0158/ARF-Shuka-Edition](https://github.com/shuka0158/ARF-Shuka-Edition).
This is a feature merge, not a replacement of the existing Unleashed/tumoflip
Sub-GHz stack. The supported boundary is documented in
[Sub-GHz Architecture](docs/subghz-architecture.md): core Sub-GHz stays in
firmware, optional decoders are loaded as `.fal` Protocol Packs, and heavy ARF
utilities stay as separate `.fap` tools on SD.

ARF protocols currently enabled in the system Sub-GHz registry:

- `Fiat SPA`
- `Suzuki`
- `Toyota`

The remaining active ARF and Shuka protocols are not linked into the core
registry. They are built from the canonical sources in `lib/subghz/protocols`
as external Protocol Packs and loaded by the normal Sub-GHz app from:

```text
/ext/apps_data/subghz/plugins/protocol_vag.fal
/ext/apps_data/subghz/plugins/protocol_kia_v0.fal
/ext/apps_data/subghz/plugins/protocol_kia_v1.fal
/ext/apps_data/subghz/plugins/protocol_kia_v2.fal
/ext/apps_data/subghz/plugins/protocol_mitsubishi_v0.fal
```

The directory also contains packs for Chrysler, Fiat Marelli, Ford v0-v3,
Kia v3-v7, Land Rover, Mazda, Porsche, PSA, Scher-Khan, Sheriff CFM, StarLine,
Subaru, and the Shuka Auto protocols. See
[Sub-GHz Protocol Packs](docs/subghz-protocol-packs.md) for the complete
inventory and source attribution.

Because loading all packs at once would exhaust RAM, Receiver settings provide
a `Protocol Pack` selector for Core, Legacy, Kia, Ford, Europe, Asia/US, and
Alarm, and Shuka Auto groups. Changing the selection safely rebuilds the
receiver and applies the new group without restarting Sub-GHz.

This preserves the normal Sub-GHz receive workflow while recovering internal
flash for Tumoflip Runtime. ProtoPirate can still provide its own isolated
implementations. The Protocol Pack loader is currently used by the graphical
system Sub-GHz app; the Sub-GHz CLI and external applications such as
ProtoPirate keep their own registries.
See [Sub-GHz Protocol Packs](docs/subghz-protocol-packs.md) for the ABI and
packaging rules.

Together with Fiat SPA, Suzuki, and Toyota in core, the Protocol Packs cover
the enabled upstream ARF registry plus the Shuka Auto decode-only additions.
BMW CAS4 and Honda Static remain disabled because they are also disabled
upstream.

Full and its tools are external `.fap` apps instead of being linked into the
core firmware image. Only the launcher is exposed in the normal Apps tree;
functional modules remain separate processes in a private data directory:

```text
/ext/apps/ARF Tools/arf_subghz_full.fap
/ext/apps/ARF Tools/arf_frequency_analyzer.fap
/ext/apps/ARF Tools/subghz_raw_edit.fap
/ext/apps/ARF Tools/keeloq_keystore_decryptor.fap
/ext/apps/ARF Tools/garage_door_remote.fap
/ext/apps_data/arf_subghz_full/modules/*.fap
```

`ARF Sub-GHz Full` is a lightweight launcher. Selecting normal Sub-GHz opens
the stable core app; dedicated ARF tools stay in separate FAPs. Loading all
standard and ARF code into one external process exceeded the device heap during
hardware testing, so the stock core Sub-GHz app remains the primary receiver
and transmitter surface.
See [ARF Sub-GHz Full](docs/arf-subghz-full.md) for the validation boundary.

ProtoPirate protocol plugins and its keystore are embedded in the internal
ProtoPirate FAP, so ARF Status no longer checks obsolete `apps_assets` paths.

## Included User Applications

- `ai_dashboard` / AI Radar:
  vendored modified version based on
  [T-Damer/flipper-ai-dashboard](https://github.com/T-Damer/flipper-ai-dashboard).
- `FlipperRelay`:
  vendored copy based on
  [squazaryu/flipper_relay](https://github.com/squazaryu/flipper_relay).
- `Authenticator` / TOTP:
  vendored modified version based on
  [akopachov/flipper-zero_authenticator](https://github.com/akopachov/flipper-zero_authenticator).
- `quac`:
  vendored modified version based on
  [rdefeo/quac](https://github.com/rdefeo/quac).
- `WiFi Mapper`:
  tumoflip Module One ESP32 Wi-Fi scan logger. See
  [WiFi Mapper](docs/wifi-mapper.md).

## BLE App Bridge and Tumoflip Runtime

tumoflip includes a BLE App Bridge service in the default Flipper BLE serial
profile. Apps can send small framed events with `app_id`, `command`, and an
optional payload. The primary companion is now
[squazaryu/TumoCompanion](https://github.com/squazaryu/TumoCompanion)
for iPhone; Mac-side workers remain optional for features that need desktop
data or compute.

The background Tumoflip Runtime adds the backward-compatible `FAB2` protocol:
request IDs, explicit response/error flags, ordered chunks, capability
discovery, and Runtime commands that do not require opening a FAP. Legacy
`FAB1` remains supported. See [the App Bridge v2 wire contract](docs/app-bridge-v2.md).

The system Sub-GHz application and all current ARF radio applications acquire
their radio through the [Radio Broker](docs/subghz-radio-broker.md). Custom
ProtoPirate and Bruteforcer loaders receive the active lease explicitly;
RollJam reports and preserves its dual-radio operation.

Release builds can emit a SHA-256 package inventory and validate the updater:

```sh
python3 tools/tumoflip/validate_release.py --write-manifest
```

GitHub release tags use the `v*` format. Pushing a release tag runs the Release
workflow, builds the updater package, runs the tumoflip release tests, validates
the manifest, generates `tumoflip-packages.json` and `tumoflip-packages.zip`,
and uploads the firmware, package manifest, package zip, SDK archive, and
SHA-256 sums to the GitHub Release.

CI release notes explicitly mark hardware validation as `not run by CI`. Before
calling a release hardware-validated, run
[Hardware Regression Checklist](docs/hardware-regression-checklist.md) on a
physical Flipper Zero and update the release notes with the cases run,
unverified cases, and any failures.

The schema v2 `tumoflip-packages.json` separates Base, ARF, Module One, and
Protocol Pack files, provides a content-addressed release ID, and supports the
host-side atomic installer with rollback. See
[Tumoflip Packages](docs/tumoflip-packages.md).

For package-only changes, such as a fixed external FAP, use the `Package
Release` workflow instead of creating a new firmware tag. It rebuilds package
resources from the selected ref, keeps the firmware version unchanged, and
replaces only `tumoflip-packages.json`, `tumoflip-packages.zip`, and the release
SHA-256 sums in the existing GitHub Release.

The standalone FlipperRelay repository lives at
[squazaryu/flipper_relay](https://github.com/squazaryu/flipper_relay).
This firmware keeps a vendored copy so tumoflip can build the app directly.

Current supported senders:

- `FlipperRelay` app: sends `sber_relay` commands such as `on`, `off`, and
  `toggle`.
- `Quac`: supports `.qab` files containing `app_id|command|payload`, for
  example `sber_relay|toggle|`.

The Mac bridge uses an explicit JSON allowlist and does not execute arbitrary
payload text from the Flipper. See
[squazaryu/flipper_relay](https://github.com/squazaryu/flipper_relay) for the
Mac bridge and app source.

## Install

Stable `main` update packages and `dev` prereleases are published on
[GitHub Releases](https://github.com/squazaryu/tumoflip/releases). The currently
selected branch build uses this artifact name:

- `flipper-z-f7-update-t-dev-089-041-002.tgz`

Before flashing, make a backup of important data:

- internal storage: `/int`
- SD card files you care about
- custom apps, configs, IR/Sub-GHz files, and settings

Install the update package with qFlipper, the Flipper mobile app, or by copying
the unpacked update folder to the SD card and running `update.fuf` on the
device.

## Build

```sh
./fbt COMPACT=1 DEBUG=0 updater_package
python3 tools/tumoflip/validate_release.py --write-manifest
```

The update package is produced under:

```text
dist/f7-C/flipper-z-f7-update-t-dev-089-041-002.tgz
```

## Upstream

This firmware is based on Unleashed:

- Upstream repository:
  [DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware)
- Original documentation:
  [documentation/](documentation/)
- Original license:
  [LICENSE](LICENSE)

Additional Sub-GHz research sources used by tumoflip:

- ARF protocol layer:
  [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF)
- Shuka Auto protocol additions:
  [shuka0158/ARF-Shuka-Edition](https://github.com/shuka0158/ARF-Shuka-Edition)

When updating tumoflip, pull/rebase from Unleashed carefully and review conflicts
around Desktop, Loader, BLE, API symbols, and bundled user applications.

## Disclaimer

This software is intended for personal and experimental use. Use it legally and
responsibly. Hardware modifications, external modules, radio features, and
custom firmware changes can affect device behavior; keep backups before testing
new builds.
