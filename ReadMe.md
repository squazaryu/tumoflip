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
- Firmware version: `tmwhflpprarf089-015`
- Firmware origin/fork: `tumoflip`
- Firmware API: `87.11`
- Target: Flipper Zero F7
- Release package: `flipper-z-f7-update-tmwhflpprarf089-015.tgz`

## Version Scheme

Installed firmware versions use this format:

```text
tmwhflpprarf089-015
```

- `tmwhflpprarf`: tumoflip firmware name shown as the installed firmware
  version prefix for the ARF-enabled build line.
- `089`: upstream Unleashed base version.
- `015`: tumoflip internal build version.

When the Unleashed base version or tumoflip internal version changes, update
the firmware version suffix in `fbt_options.py`, release notes, README, and the
published update package name together.

## tumoflip Changes

- Rebranded firmware origin to `tumoflip` and distribution/version suffix to
  `tmwhflpprarf089-015`.
- Added custom Desktop main menu styles inspired by Momentum-style layouts.
- Added `8/1` Module One folder after Apps in the Desktop OK menu.
- Added Module One icon based on the Rotten Mechanism cross mark.
- Removed Dummy Mode and related shortcuts.
- Added Settings entry to the Desktop short-Up quick menu.
- Added BLE App Bridge support.
- Added `FlipperRelay` host bridge tooling for Mac-side BLE automations.
- Added an initial ARF Sub-GHz protocol layer from
  [D4C1-Labs/Flipper-ARF](https://github.com/D4C1-Labs/Flipper-ARF).
- Added ARF Sub-GHz `setting_user` frequencies, hopper frequencies, and custom
  presets as ProtoPirate-only assets, isolated from the normal Sub-GHz app.
- Added ProtoPirate as an external app for Module One SD deployment.
- Vendored local user applications into `applications_user` so the repository
  can be built without absolute local symlinks.

## Differences from Unleashed

tumoflip is based on Unleashed, but it intentionally changes the parts of the
firmware that affect the Desktop, quick access flow, bundled apps, and build
identity.

| Area | Unleashed | tumoflip |
| --- | --- | --- |
| Firmware identity | Reports itself as Unleashed. | Reports `firmware_version: tmwhflpprarf089-015` and `firmware_origin_fork: tumoflip`. |
| Desktop layouts | Uses the default Unleashed Desktop style set. | Adds custom main menu styles, including Wii, DSi, Vertical, and Wii Vertical variants. |
| Dummy Mode | Included and reachable from Desktop shortcuts. | Removed from firmware and removed from shortcuts. |
| Short-Up quick menu | Includes the standard quick actions, including Dummy Mode in the original layout. | Replaces the removed Dummy Mode shortcut with Settings. |
| Desktop OK menu | Uses the standard app/menu layout. | Adds an `8/1` folder immediately after Apps for Module One related apps. |
| Module One access | Apps are reached through the normal Apps tree. | Provides a dedicated `8/1` launcher folder with a custom cross icon and SD-deployed Module One apps. |
| Settings return flow | Standard Unleashed navigation. | Keeps the Desktop Settings shortcut separate from the normal OK menu flow where possible. |
| BLE services | Standard Unleashed BLE behavior. | Adds BLE App Bridge support for local app communication and Mac-side command routing. |
| ARF protocols | Not included. | Adds a size-limited initial ARF Sub-GHz protocol set while keeping Unleashed/tumoflip protocols intact. |
| User apps | External/local apps are not part of the base repository. | Vendors selected local apps into `applications_user` so the firmware builds reproducibly. |
| Build metadata | Uses upstream build metadata conventions. | Uses `tmwhflpprarf089-015` for the installed firmware version and release artifact suffix, while keeping `tumoflip` as the fork origin. |

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

Currently enabled ARF protocols:

- `VAG`
- `Fiat SPA`
- `Kia v0`
- `Kia v1`
- `Kia v2`
- `Suzuki`
- `Mitsubishi`
- `Toyota`

Additional ARF protocol sources are present in the tree but are not registered
in the firmware menu yet. A full ARF protocol registry exceeded the safe
updater package size and triggered a C2/radio-region overlap warning during
build. Deferred protocols include Ford variants, PSA, Porsche Cayenne,
StarLine, Scher-Khan, Sheriff CFM, Land Rover, Subaru, Mazda variants, Chrysler,
Fiat Marelli, and later Kia variants.

ProtoPirate is built as an external `.fap` instead of being linked into the
core firmware image. The intended SD location is:

```text
/ext/apps/module one/sub-ghz/ProtoPirate.fap
```

Its runtime plugin assets and keystore are deployed to:

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

## BLE App Bridge and FlipperRelay

tumoflip includes a BLE App Bridge service in the default Flipper BLE serial
profile. Apps can send small framed events with `app_id`, `command`, and an
optional payload. A paired Mac can listen for those events and run local
commands that are configured on the Mac.

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

- `flipper-z-f7-update-tmwhflpprarf089-015.tgz`

Before flashing, make a backup of important data:

- internal storage: `/int`
- SD card files you care about
- custom apps, configs, IR/Sub-GHz files, and settings

Install the update package with qFlipper, the Flipper mobile app, or by copying
the unpacked update folder to the SD card and running `update.fuf` on the
device.

## Build

```sh
./fbt COMPACT=1 DEBUG=0 COPRO_DISCLAIMER="--I-understand-what-I-am-doing=yes" updater_package
```

The update package is produced under:

```text
dist/f7-C/flipper-z-f7-update-tmwhflpprarf089-015.tgz
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
