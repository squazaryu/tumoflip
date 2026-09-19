# Inspector/Status and September upstream adaptation

Unreleased source integration. Published Stable 008 and Dev 009-002 assets/tags
are unchanged. Do not install a locally measured archive merely because its
distribution suffix matches the old release: exact commit identity matters.

## Baseline and scope

Stable ancestry `e2cf3ba8` was merged explicitly into the working branch without
replacing the Dev 009 suffix, LTO configuration or public API with stable values.
The stable KeeLoq size refactor was already present in Dev. README now identifies
Stable 008/v1.0.8/API 88.7 correctly. This resolves the local lineage/metadata
portion of #484; remote branches and issue acceptance are separate.

- Unleashed `370ca57bc3`: deep-content equality for FeliCa systems and DESFire
  applications/file data. The SimpleArray byte-length fix was already present.
- Unleashed `7db0c26d05`: stop MIFARE Classic poller/phase continuation when all
  sectors and keys are read. Preserve the CUID iteration and pre-activation Skip
  safeguards; a zero-sector/unidentified card does not qualify as complete.
- Unleashed `37e7db36f6`: Monarch/KEY received-field recognition and stored Monarch
  discriminator. No new generation/manual-entry/TX support. Existing Monarch
  behavior remains; newly recognized variants are rejected before encoder upload.
- ARF `da08b6120e`: opt-in per-receiver AM/FM filtering, adapted to Standard,
  ARF module receivers, protocol-pack recreation and Standard AUTO diversity.
  Unknown modulation, RAW and old callers keep the unfiltered behavior.
  The preset parser uses bounded CC1101 register data, not custom display labels.

## API and packages

API **88.10** adds `subghz_receiver_set_modulation_filter` without removing old
exports. Old clients leave the optional gate disabled. Rebuilt ARF modules that
import this symbol require the paired firmware; do not install those binaries
on 88.9 just because the API major matches. APPCHK/import validation remains
mandatory. The numeric API does not imply feature parity with another firmware.

`capture_inspector.fap` is a package-only ARF-group item at
`apps_data/arf_subghz_full/packages/capture_inspector.fap`, not resident core code.
It is included in the local paired package export. Publishing or changing the
independent FW Packages catalog is not part of this local implementation.

## Capture Inspector

Open **Standard Sub-GHz → Saved → a capture → Inspect / Compare**, or use the ARF
hub entry. A missing package produces an installation message. Standard exits
before launching the FAP and returns without reopening the transmitter.

- Inspect saved `.sub`/`.psf` key-file fields; select A and B, compare, optionally
  export a text report. No RF reception/transmission, secret recovery or CRC claim.
- Supported: key-file version 1, up to 64 KiB, 24 fields, 31-character field names
  and 159-character values. RAW uses the existing RAW tools instead.
- Unique field order is ignored; repeated unknown names match by occurrence.
  Unknown fields are retained. Invalid/oversize input is rejected, not truncated.
- Source files are opened read-only. Reports use CREATE_NEW under
  `apps_data/capture_inspector/compare_NNN.txt`; writes, sync and close are checked.
  Partial report cleanup failure remains visible even after cancellation.
- Back cancels background work. Export contains snapshot values, not a fresh
  source-file read, and may include private capture metadata.

## ARF Status

Reports bounded ELF/manifest metadata, target, API, app version and file size for
managed modules and all 31 managed protocol packs. It never executes a scanned
app or extracts its assets. Missing, malformed, busy, wrong-target and API-mismatch
files are distinguished. Extra custom files are outside the managed inventory.

**Header compatible does not mean verified.** Hash, resolved imports, execution
and hardware acceptance remain explicitly unchecked. No trusted hash baseline
is manufactured from files already on the device.

## Verification and hardware checklist

Host regressions compile the actual changed C routines with ASan/UBSan and cover
nested equality, polling completion, preset parsing, independent receiver gates,
capture parsing/storage/export failures and malformed ELF metadata. Native GUI
previews use production Submenu/TextBox/U8g2 draw functions; they are not a device
interaction test. Build with the pinned Flipper 39/GCC 12.3 toolchain and `-j2`.

Before release, on the user's own test captures/cards:

- [ ] Read complete and partial MIFARE Classic cards; Skip before activation must
  keep the loaded dump. Confirm existing CUID behavior and dictionary saves.
- [ ] Copy/save/reopen FeliCa and DESFire dumps; unchanged data must compare equal,
  altered nested data unequal, with no false shadow updates.
- [ ] Inspect saved Monarch/KEY recordings; verify metadata round trips. This
  candidate adds no new transmit acceptance.
- [ ] Receive AM and FM through internal/external radios, AUTO diversity and
  preset hopping; retry custom presets and RAW Auto Decode across protocol packs.
- [ ] Open Inspector through both entries, select/cancel both files, compare,
  scroll long fields, export, cancel and remove the SD during file work.
- [ ] Exit Inspector and confirm Standard does not enter transmit. Inspect Status
  with a missing package, corrupted copy, busy file and newer-API manifest.
- [ ] Smoke-test NFC, RFID, SD, Bluetooth/TumoCompanion and existing FAP loading.

No hardware-only checkbox is marked by a host test or successful build.

## Memory interpretation

Compare identical COMPACT=1/DEBUG=0/LTO=1 toolchains and the published 009-002
baseline in `.ecc/benchmarks/arf-inspector-status-before.json`. Report core flash,
section gap, page-aligned C2 gap and FAP storage separately. `.free_flash` includes
radio-reserved space and is not the usable firmware growth margin. FAP code uses
SD and runtime RAM; moving new work into a FAP avoids resident growth but does not
remove existing core functionality or guarantee a net flash saving.

ARF XIP/library migration, optional RX preset distribution and new experimental
encoder/counter behavior are not part of this implementation.
