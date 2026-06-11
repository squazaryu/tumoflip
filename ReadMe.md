# tumoflip

Custom Flipper Zero firmware based on
[DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware).

This repository keeps the Unleashed firmware history and adds a small set of
personal firmware changes on top of it. It is not an official Unleashed release
and is not affiliated with Flipper Devices or the Unleashed team.

## Current Build

- Base: Unleashed firmware local dev tree
- Firmware name/origin: `tumoflip`
- Firmware API: `87.9`
- Target: Flipper Zero F7
- Release package:
  [v0.1.1](https://github.com/squazaryu/tumoflip/releases/tag/v0.1.1)

## tumoflip Changes

- Rebranded firmware origin and distribution suffix to `tumoflip`.
- Added custom Desktop main menu styles inspired by Momentum-style layouts.
- Added `8/1` Module One folder after Apps in the Desktop OK menu.
- Added Module One icon based on the Rotten Mechanism cross mark.
- Removed Dummy Mode and related shortcuts.
- Added Settings entry to the Desktop short-Up quick menu.
- Added BLE App Bridge support.
- Vendored local user applications into `applications_user` so the repository
  can be built without absolute local symlinks.

## Differences from Unleashed

tumoflip is based on Unleashed, but it intentionally changes the parts of the
firmware that affect the Desktop, quick access flow, bundled apps, and build
identity.

| Area | Unleashed | tumoflip |
| --- | --- | --- |
| Firmware identity | Reports itself as Unleashed. | Reports `firmware_version: tumoflip` and `firmware_origin_fork: tumoflip`. |
| Desktop layouts | Uses the default Unleashed Desktop style set. | Adds custom main menu styles, including Wii, DSi, Vertical, and Wii Vertical variants. |
| Dummy Mode | Included and reachable from Desktop shortcuts. | Removed from firmware and removed from shortcuts. |
| Short-Up quick menu | Includes the standard quick actions, including Dummy Mode in the original layout. | Replaces the removed Dummy Mode shortcut with Settings. |
| Desktop OK menu | Uses the standard app/menu layout. | Adds an `8/1` folder immediately after Apps for Module One related apps. |
| Module One access | Apps are reached through the normal Apps tree. | Provides a dedicated `8/1` launcher folder with a custom cross icon. |
| Settings return flow | Standard Unleashed navigation. | Keeps the Desktop Settings shortcut separate from the normal OK menu flow where possible. |
| BLE services | Standard Unleashed BLE behavior. | Adds BLE App Bridge support for local app communication. |
| User apps | External/local apps are not part of the base repository. | Vendors selected local apps into `applications_user` so the firmware builds reproducibly. |
| Build metadata | Uses upstream build metadata conventions. | Uses `tumoflip` for distribution suffix, firmware origin, Git origin, and release artifacts. |

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

## Included User Applications

- `ai_dashboard`
- `flipper_relay`
- `quac`

## Install

Download the latest update package from
[GitHub Releases](https://github.com/squazaryu/tumoflip/releases):

- `flipper-z-f7-update-tumoflip.tgz`

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
dist/f7-C/flipper-z-f7-update-tumoflip.tgz
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
