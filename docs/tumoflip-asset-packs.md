# Tumoflip Asset Packs

Tumoflip has a narrow SD-based asset-pack layer for visual overrides that do
not need to be linked into the firmware image. The first supported slice is
limited to icons used by custom Desktop OK-menu entries.

This is intentionally smaller than Momentum asset packs. It does not replace
fonts, animations, or global UI resources.

## Layout

Select the active pack by writing its directory name to:

```text
/ext/apps_data/tumoflip/asset_packs/active.txt
```

The name may contain only letters, digits, `_`, and `-`. This prevents path
traversal and keeps the loader deterministic.

A pack stores icons under:

```text
/ext/apps_data/tumoflip/asset_packs/<pack>/manifest.txt
/ext/apps_data/tumoflip/asset_packs/<pack>/Icons/
```

The manifest is mandatory. It uses FlipperFormat:

```text
Filetype: Tumoflip Asset Pack
Version: 1
Name: <pack>
Target: desktop-ok-menu
ModuleOneIcon: ModuleOne_14.bmx
```

The loader accepts only `Version: 1` and `Target: desktop-ok-menu`. The manifest
`Name` must match the selected directory from `active.txt`. This keeps the
current implementation scoped to Desktop OK-menu icons and prevents accidental
loading of unrelated asset directories.

Currently supported active files:

```text
ModuleOne_14.bmx
```

`ARFToolsIcon: ARFTools_14.bmx` and `ARFTools_14.bmx` are legacy optional
entries. Existing generated packs may keep them, but current firmware ignores
that icon slot and uses the Module One icon slot for the Desktop item that opens
Module One Cockpit.

## Icon Format

The `.bmx` file is a small tumoflip wrapper around a single firmware icon
frame:

```text
uint32_t width
uint32_t height
uint8_t  frame_payload[]
```

For this first slice, `width` and `height` must both be `14`. The frame payload
must be no larger than 256 bytes. Invalid, missing, oversized, or incomplete
files are ignored and the built-in firmware icon is used instead.

## Runtime Behavior

The loader reads the active pack lazily when the Desktop OK menu opens. Loaded
icons live only while that menu is open and are freed when it closes.

Fallback rules:

- no SD card: built-in icons
- no `active.txt`: built-in icons
- invalid pack name: built-in icons
- missing or invalid manifest: built-in icons
- missing icon in a valid pack: built-in icon for that menu item
- broken icon file: built-in icon for that menu item

The layer does not run during boot and does not change Desktop, Loader, Apps,
or Settings behavior unless a valid active pack provides a supported icon.

## Build a Pack

Use the helper script to generate a valid SD layout from 14x14 PNG icons:

```bash
python3 tools/tumoflip/make_asset_pack.py tumo-default
```

By default this writes:

```text
build/tumoflip-asset-packs/active.txt
build/tumoflip-asset-packs/tumo-default/manifest.txt
build/tumoflip-asset-packs/tumo-default/Icons/ModuleOne_14.bmx
build/tumoflip-asset-packs/tumo-default/Icons/ARFTools_14.bmx
```

The generated `ARFTools_14.bmx` file is kept for compatibility with older dev
builds.

If the SD card is mounted on macOS, the script can write directly into the
Flipper SD root:

```bash
python3 tools/tumoflip/make_asset_pack.py tumo-default --ext-root /Volumes/FLIPPER
```

Custom PNG sources are also supported:

```bash
python3 tools/tumoflip/make_asset_pack.py tumo-custom \
  --module-one path/to/module_one_14.png \
  --arf-tools path/to/arf_tools_14.png
```
