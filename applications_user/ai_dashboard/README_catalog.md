# AI Radar

AI Radar is a Flipper Zero dashboard for tracking AI provider usage windows on-device.

## Features

- Main overview screen for all visible providers
- Per-provider detail screens
- Settings for overview visibility, autoscroll, and display dimming
- USB snapshot sync from a host machine
- BLE snapshot sync support for host-to-Flipper updates

## Companion Bridge

The Flipper app reads a compact snapshot file from the Flipper app data path:
/ext/apps_data/ai_dashboard/usage.txt

The companion bridge script can:

- collect Codex usage through the local Codex app-server
- parse Claude usage through the local Claude usage command
- merge manual provider snapshots
- push snapshots to the Flipper over USB or BLE

## Controls

- Left / Right: switch between overview and provider screens
- Up / Down: scroll the overview or switch provider subpages
- OK: open settings
- Back: leave settings or exit the app
