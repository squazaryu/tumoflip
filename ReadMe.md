# tumoflip

Custom Flipper Zero firmware based on
[DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware).

This repository keeps the Unleashed firmware history and adds a small set of
personal firmware changes on top of it. It is not an official Unleashed release
and is not affiliated with Flipper Devices or the Unleashed team.

This firmware contains experimental features and local changes. Some of them
may be unstable, incomplete, or incompatible with future Unleashed updates. If
you find a tumoflip-specific issue, report it in this repository:
[squazaryu/tumoflip issues](https://github.com/squazaryu/tumoflip/issues).

## Current Build

- Base: Unleashed 089 with selected upstream dev updates
- Firmware version: `tmwhflpprarf089-020`
- Firmware origin/fork: `tumoflip`
- Firmware API: `87.14`
- Target: Flipper Zero F7
- Release package: `flipper-z-f7-update-tmwhflpprarf089-020.tgz`

## Version Scheme

Installed firmware versions use this format:

```text
tmwhflpprarf089-020
```

- `tmwhflpprarf`: tumoflip firmware name shown as the installed firmware
  version prefix for the ARF-enabled build line.
- `089`: upstream Unleashed base version.
- `020`: tumoflip internal build version.

When the Unleashed base version or tumoflip internal version changes, update
the firmware version suffix in `fbt_options.py`, release notes, README, and the
published update package name together.

## tumoflip Changes

- Rebranded firmware origin to `tumoflip` and distribution/version suffix to
  `tmwhflpprarf089-020`.
- Added custom Desktop main menu styles inspired by Momentum-style layouts.
- Added `8/1` Module One folder after Apps in the Desktop OK menu.
- Replaced the Desktop OK menu `Sub-GHz Remote` shortcut with an `ARF Tools`
  folder shortcut.
- Added Module One icon based on the Rotten Mechanism cross mark.
- Removed Dummy Mode and related shortcuts.
- Added Settings entry to the Desktop short-Up quick menu.
- Added BLE App Bridge support.
- Added `FlipperRelay` host bridge tooling for Mac-side BLE automations.
- Added an initial ARF Sub-GHz protocol layer from
  [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF).
- Added ARF Sub-GHz `setting_user` frequencies, hopper frequencies, and custom
  presets as ProtoPirate-only assets, isolated from the normal Sub-GHz app.
- Added ProtoPirate and ARF Tools as external apps for isolated SD deployment.
- Added MIFARE Ultralight/NTAG PWD and PACK to the NFC read-success screen.
- Added the Bambu Lab filament spool NFC parser.
- Added adaptive dwell and signal hold to hopping in the system Sub-GHz app.
- Added external Sub-GHz Protocol Packs so selected decoders can be loaded from
  SD without keeping a second copy in the core firmware image.
- Added a Sub-GHz Radio Broker for exclusive radio ownership, external CC1101
  power ownership, and internal-device fallback in system Sub-GHz and all
  current ARF radio applications.
- Added a release validator and versioned package manifest for reproducible SD
  app layouts and independent C2/updater safety checks.
- Vendored local user applications into `applications_user` so the repository
  can be built without absolute local symlinks.

## Differences from Unleashed

tumoflip is based on Unleashed, but it intentionally changes the parts of the
firmware that affect the Desktop, quick access flow, bundled apps, and build
identity.

| Area | Unleashed | tumoflip |
| --- | --- | --- |
| Firmware identity | Reports itself as Unleashed. | Reports `firmware_version: tmwhflpprarf089-020` and `firmware_origin_fork: tumoflip`. |
| Desktop layouts | Uses the default Unleashed Desktop style set. | Adds custom main menu styles, including Wii, DSi, Vertical, and Wii Vertical variants. |
| Dummy Mode | Included and reachable from Desktop shortcuts. | Removed from firmware and removed from shortcuts. |
| Short-Up quick menu | Includes the standard quick actions, including Dummy Mode in the original layout. | Replaces the removed Dummy Mode shortcut with Settings. |
| Desktop OK menu | Uses the standard app/menu layout. | Keeps the `8/1` Module One folder after Apps and replaces the `Sub-GHz Remote` shortcut with `ARF Tools`. |
| ARF tools access | Apps are reached through the normal Apps tree. | Provides a dedicated `ARF Tools` launcher folder with SD-deployed ARF/ProtoPirate apps. |
| Settings return flow | Standard Unleashed navigation. | Keeps the Desktop Settings shortcut separate from the normal OK menu flow where possible. |
| BLE services | Standard Unleashed BLE behavior. | Adds BLE App Bridge support for local app communication and Mac-side command routing. |
| ARF protocols | Not included. | Keeps the core set size-limited and loads selected automotive decoders from SD as Protocol Packs. |
| Sub-GHz hopping | Frequency hopping only. | Adds preset and combined hopping plus an adaptive scan dwell, signal hold, post-signal grace period, and bounded hold time to system Sub-GHz. |
| NFC additions | Uses the Unleashed 089 NFC feature set. | Shows captured MIFARE Ultralight/NTAG PWD and PACK and adds the Bambu Lab filament spool parser. |
| User apps | External/local apps are not part of the base repository. | Vendors selected local apps into `applications_user` so the firmware builds reproducibly. |
| Build metadata | Uses upstream build metadata conventions. | Uses `tmwhflpprarf089-020` for the installed firmware version and release artifact suffix, while keeping `tumoflip` as the fork origin. |

## Notes on Custom UI

The custom Desktop styles are focused on changing the main Desktop launcher
experience, not replacing every nested app list in the firmware. Apps and
system screens still mostly follow the underlying Unleashed UI behavior unless
they were changed explicitly.

Current custom Desktop modes:

- Wii
- Wii Vertical
- DSi
- Vertical

## ARF Sub-GHz Layer

tumoflip includes an initial merge of selected automotive Sub-GHz protocol code
from [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF). This is
a feature merge, not a replacement of the existing Unleashed/tumoflip Sub-GHz
stack.

ARF protocols currently enabled in the system Sub-GHz registry:

- `Fiat SPA`
- `Suzuki`
- `Toyota`

The other 24 active ARF protocols are not linked into the core registry. They
are built from the canonical sources in `lib/subghz/protocols` as external
Protocol Packs and loaded by the normal Sub-GHz app from:

```text
/ext/apps_data/subghz/plugins/protocol_vag.fal
/ext/apps_data/subghz/plugins/protocol_kia_v0.fal
/ext/apps_data/subghz/plugins/protocol_kia_v1.fal
/ext/apps_data/subghz/plugins/protocol_kia_v2.fal
/ext/apps_data/subghz/plugins/protocol_mitsubishi_v0.fal
```

The directory also contains packs for Chrysler, Fiat Marelli, Ford v0-v3,
Kia v3-v7, Land Rover, Mazda, Porsche, PSA, Scher-Khan, Sheriff CFM, StarLine,
and Subaru. See [Sub-GHz Protocol Packs](docs/subghz-protocol-packs.md) for the
complete inventory.

Because loading all packs at once would exhaust RAM, Receiver settings provide
a `Protocol Pack` selector for Core, Legacy, Kia, Ford, Europe, Asia/US, and
Alarm groups. The selected group is applied the next time Sub-GHz starts.

This preserves the normal Sub-GHz receive workflow while recovering internal
flash for Tumoflip Runtime. ProtoPirate can still provide its own isolated
implementations. The Protocol Pack loader is currently used by the graphical
system Sub-GHz app; the Sub-GHz CLI continues to use the built-in registry.
See [Sub-GHz Protocol Packs](docs/subghz-protocol-packs.md) for the ABI and
packaging rules.

Together with Fiat SPA, Suzuki, and Toyota in core, the Protocol Packs cover
all protocols enabled in the upstream ARF registry. BMW CAS4 and Honda remain
disabled because they are also disabled upstream.

ProtoPirate and the lightweight ARF Status diagnostic app are built as
external `.fap` apps instead of being linked into the core firmware image.
Functional ARF tools should stay as separate `.fap` apps in the same launcher
folder rather than being hidden inside the status helper. Current intended SD
locations are:

```text
/ext/apps/ARF Tools/proto_pirate.fap
/ext/apps/ARF Tools/arf_subghz.fap
/ext/apps/ARF Tools/arf_subghz_full.fap
/ext/apps/ARF Tools/arf_keeloq.fap
/ext/apps/ARF Tools/arf_counter_bf.fap
/ext/apps/ARF Tools/arf_car_emulate.fap
/ext/apps/ARF Tools/arf_frequency_analyzer.fap
/ext/apps/ARF Tools/arf_psa_decrypt.fap
/ext/apps/ARF Tools/rolljam.fap
/ext/apps/ARF Tools/subghz_bruteforcer.fap
/ext/apps/ARF Tools/arf_status.fap
```

ProtoPirate runtime plugin assets and keystore are deployed to:

```text
/ext/apps_assets/proto_pirate
```

The app reads its ARF frequency list from:

```text
/ext/apps_assets/proto_pirate/setting_user
```

This keeps ARF frequencies, hopper frequencies, and custom presets isolated
from the normal Sub-GHz app. The shared `/ext/subghz/assets/setting_user` file
is intentionally not used for ARF.

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

## BLE App Bridge and Tumoflip Runtime

tumoflip includes a BLE App Bridge service in the default Flipper BLE serial
profile. Apps can send small framed events with `app_id`, `command`, and an
optional payload. The primary companion is now
[squazaryu/unleashed-companion](https://github.com/squazaryu/unleashed-companion)
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

The schema v2 `tumoflip-packages.json` separates Base, ARF, Module One, and
Protocol Pack files, provides a content-addressed release ID, and supports the
host-side atomic installer with rollback. See
[Tumoflip Packages](docs/tumoflip-packages.md).

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

Download the latest update package from
[GitHub Releases](https://github.com/squazaryu/tumoflip/releases):

- `flipper-z-f7-update-tmwhflpprarf089-020.tgz`

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
dist/f7-C/flipper-z-f7-update-tmwhflpprarf089-020.tgz
```

## Upstream

This firmware is based on Unleashed:

- Upstream repository:
  [DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware)
- Original documentation:
  [documentation/](documentation/)
- Original license:
  [LICENSE](LICENSE)

When updating tumoflip, pull/rebase from Unleashed carefully and review conflicts
around Desktop, Loader, BLE, API symbols, and bundled user applications.

## Disclaimer

This software is intended for personal and experimental use. Use it legally and
responsibly. Hardware modifications, external modules, radio features, and
custom firmware changes can affect device behavior; keep backups before testing
new builds.
