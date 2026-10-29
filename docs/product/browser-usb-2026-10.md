# Browser reliability and field delivery — implementation candidate

Base: published Dev009-033 (`513a55a3615f5d80e2ecb029a92f8e3c08d26e1a`).
This source change is not a new firmware/FW Packages/iOS release, Stable
promotion, installation or physical acceptance. Keep version publication separate.

## Adapted boundaries

- Unleashed browser `a23fea8740ed8bcfc6fe31feee5451340fefacba`: own icon
  buffers, reuse allocations, safe self-assignment, selection-window bounds,
  null-aware scene/menu actions, validator cleanup and a128-byte text-read buffer.
- GUI `33514cb3ce1b5ba47dc4d2821c1b2f7e7e8fe415`: assign every compressed text
  baseline, including zero spare pixels; separate filenames in delete dialogs.
  Core/ARF shared delete scenes stay synchronized. Native rendering additionally
  exposed and fixed clipping of10-byte NFC UIDs and large USB progress counters.
- Dooya `5178d39cf0413443d4834557277ec5c3bf2f954d`: Up/Down use held commands,
  Left/Right use step commands, OK restores the captured command. Existing Repeat,
  upload timing and cancellation stay intact. Button override is honored. Unlike
  the upstream port, selecting a command does not rewrite the saved capture's Key.
- USB CD-ROM0.2: see `applications_user/usb_cdrom/UPSTREAM.md`. Base FW Package
  only, read-only images, checked SCSI/BOT boundaries, GPL notice inside FAP assets.
  USB Floppy, CFS, AI Composer, ARF tree rewrites and Marauder attack profiles
  are not copied as part of this selected batch.

## Checked favorites, without extra resident services

Only the edit operation is lazy: native resources must contain
`apps_data/archive/plugins/archive_favorites.fal`, private host ABI1. Reading
favorites remains in core. Missing/incompatible/OOM plugin mapping refuses an edit
and leaves the existing list untouched. The updater validator requires the exact
writer in `resources.ths`. Install native resources with the next firmware.

Edits stream to CREATE_NEW `favorites.tmp`, check read/seek/write/sync/close,
back up and read-back verify the old list before replacement, verify the new
contents and attempt rollback on failure. Pre-existing temp/backup files are
never overwritten. A failed rollback retains `favorites.txt.tumo-bak` for manual
recovery. This is not an atomic power-loss transaction. The foreground caller
serializes edits; external simultaneous RPC editing of favorites is not supported.
Successful replacement is not reported failed solely because temp cleanup failed.
Delete/rename match an entire stored path, not a substring. Empty lists are valid.
No Sub-GHz capture file is deleted or rewritten by favorites edits.

## Verification boundary

- Full firmware host discovery:1279 tests passed after updating package fixtures.
- Actual C regressions: fatal ASan/UBSan for icon ownership, selection, text
  placement, Dooya upload generation, SCSI and in-memory favorites I/O failures.
- F7 COMPACT1/DEBUG0, `-j2`; API88.23 unchanged. ELF/DfuSe C1 end
  `0x080D6000`, section gap4096 and full physical erase reserve4096 bytes in
  the implementation candidate. Final release SHA/version must be revalidated.
- Six native-font frames execute the production NFC scene/CD-ROM draw callback.
  iOS field-report implementation and native simulator checks are separate.
- Independent agent/human review, physical USB/PWM/RF/SD acceptance and proof of
  the historical idle OOM cause have not been established by this work.

After delivery test on disposable copies: deep/empty/large folders, icon changes,
pin/unpin/rename/reorder and failures, long UID/names, own Dooya controller
commands/OK/Back, ISO attach/read/eject/Back/unplug and USB RPC restoration.
Never inject storage failures against the user's only captures.
