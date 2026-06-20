# Tumoflip Packages

Tumoflip release validation produces a schema v2 sidecar
`tumoflip-packages.json` next to
`update.fuf`. It records firmware identity, API version, safety margins, update
artifact hashes, and SD files grouped as:

- `base`
- `arf`
- `module_one`
- `protocol_packs`

Every SD entry contains its source path, `/ext` target path, byte size, and
SHA-256. This is the input contract for future companion-side atomic install
and rollback support; the current firmware updater still installs its normal
resource archive.

The manifest also includes a content-addressed `release_id` and a `cleanup`
list. A package client must verify every staged SHA-256 before replacing files,
and may remove a legacy path only after its canonical replacement is present.
Legacy cleanup deliberately remains host-side so a one-time SD migration does
not consume constrained firmware flash on every device.

Run after building the updater:

```sh
python3 tools/tumoflip/validate_release.py --write-manifest
```

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

Validation fails when:

- updater or radio CRC differs from `update.fuf`;
- target or firmware version is inconsistent;
- updater exceeds 128 KiB;
- firmware leaves less than 8 KiB before the C2/radio region;
- the Protocol Pack set is incomplete or unexpected;
- an ARF app is duplicated under `apps/Sub-GHz`;
- the canonical `apps/ARF Tools` package is incomplete.

Local application trees can be excluded from distribution with
`EXCLUDED_EXT_APPS` in `fbt_options.py`. This prevents an ignored experimental
`application.fam` from entering release resources accidentally.
