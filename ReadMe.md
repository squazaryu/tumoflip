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
  [v0.1.0](https://github.com/squazaryu/tumoflip/releases/tag/v0.1.0)

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
