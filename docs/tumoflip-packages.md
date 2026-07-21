# Tumoflip Packages

Tumoflip release validation produces a schema v2 sidecar
`tumoflip-packages.json` next to
`update.fuf`. It records firmware identity, API version, safety margins, update
artifact hashes, and SD files grouped as:

- `base`
- `arf`
- `module_one` (`Tumo IR Lab`, `tumoflip_xremote.fap`, `ESP32 Marauder`, and
  `WiFi Mapper`)
- `protocol_packs`

Every SD entry contains its source path, `/ext` target path, byte size, SHA-256,
and a lowercase MD5 used for low-cost device-side reconciliation. SHA-256 remains
the integrity digest. The release validator also emits
`tumoflip-packages.zip`, containing exactly those source files. Unleashed
Companion uses both assets for verified, transactional SD installation and
rollback; the firmware updater continues to install its normal resource archive
independently.

The manifest also includes a content-addressed `release_id` and a `cleanup`
list. A package client must verify every staged SHA-256 before replacing files,
and may remove a legacy path only after its canonical replacement is present.
Legacy cleanup deliberately remains host-side so a one-time SD migration does
not consume constrained firmware flash on every device.

The `md5` field is optional for schema v2 compatibility. A client may use it to
adopt files installed by the firmware resource updater only after every file in
the selected group matches its manifest digest. Presence-only checks and partial
group matches must remain `Need update`. Manifests without `md5` retain the
conservative transaction-ledger behavior.

Run after building the updater. This writes both the manifest and package ZIP:

```sh
python3 tools/tumoflip/validate_release.py --write-manifest
```

Package-only updates are used when only SD content changes: external `.fap`
apps, `.fal` Protocol Packs, assets, or other `/ext` resources. They keep the
installed firmware version unchanged and emit a manifest with
`package_release.type = "package-only"`:

```sh
python3 tools/tumoflip/package_release.py \
  --build-dir build/f7-firmware-C \
  --target-release-tag v0.3.1
```

Before writing the package manifest, the package-only builder syncs current
`build/f7-firmware-C/.extapps/*.fap` exports into the release resources tree.
This makes targeted FAP fixes, such as WiFi Mapper, publishable through FW
Packages without rebuilding or reinstalling flash firmware.

Use the `Package Release` GitHub Actions workflow to publish updated
`tumoflip-packages.json`, `tumoflip-packages.zip`, and refreshed SHA-256 sums to
an existing firmware release. The workflow downloads the already-published
firmware assets for that tag, hashes those unchanged files together with the new
package assets, and does not create or upload new firmware artifacts.

Apply all package groups to a directly mounted SD card:

```sh
python3 tools/tumoflip/apply_packages.py \
  dist/f7-C/f7-update-*/tumoflip-packages.json \
  build/f7-firmware-C/resources \
  /Volumes/FLIPPER
```

Use `--dry-run` to verify the manifest and every source hash without modifying
the card. Successful installs write `/.tumoflip/install-state.json` and retain
replaced files under `/.tumoflip/rollback/<transaction>`.

Successful installs also write `/.tumoflip/package-state.txt`, a compact
FlipperFormat-compatible state file intended for future firmware-side package
audits. Version 1 records the package schema, release id, transaction id,
firmware version/API, package release id, installed groups, installed file
count, cleanup candidate count, and rollback path. It is read-only status for
firmware and companion diagnostics; it is not an instruction to delete or
overwrite files.

Firmware exposes the first read-only audit through FAB2 `runtime/status`:

```text
...;transfer=0;sd=1;pkg=1;sid=00000000;...
```

Current Runtime v2 status also includes `sd=0/1`. `sd=1;pkg=1` means the SD card
is ready and the state file is present. `sd=1;pkg=0` means the SD card is ready
but no package transaction has been recorded yet. `sd=0;pkg=0` means package
state cannot be trusted until external storage is ready. This firmware pass does
not parse package fields, hash SD files, or delete stale duplicates.

For full on-device inspection, open `Apps` -> `Tools` -> `Tumoflip Packages`.
This external FAP keeps the flash-critical runtime small while still running on
the Flipper itself. It reads `/.tumoflip/package-state.txt` and
`/.tumoflip/install-state.json`, verifies each recorded package target with
SHA-256, reports missing or mismatched files, and flags known legacy ARF paths
for review. It is intentionally read-only and never deletes or overwrites SD
content.

Validation fails when:

- updater or radio CRC differs from `update.fuf`;
- target or firmware version is inconsistent;
- updater exceeds 128 KiB;
- firmware leaves less than one 4 KiB erase page before the C2/radio region;
- the Protocol Pack set is incomplete or unexpected;
- an ARF app is duplicated under `apps/Sub-GHz`;
- the canonical `apps/ARF Tools` package is incomplete.

Local application trees can be excluded from distribution with
`EXCLUDED_EXT_APPS` in `fbt_options.py`. This prevents an ignored experimental
`application.fam` from entering release resources accidentally.
