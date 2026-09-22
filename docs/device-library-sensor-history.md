# Device Library, sensor hypotheses and file history — capability contract

This extends the approved Work Profiles / Decoder References candidate. It is
not permission to publish: show the new native GUI previews before release.

## Capability and constraints

- Device Library is a FW Package, not resident UI. Cards contain a name, notes,
  tags and up to eight SD file references; original files are never moved/copied
  just because a card links them. Known capture types can be opened explicitly in
  their normal app. Scripts/macros are references only, never executed implicitly.
- Sensor Helper extends a four-capture Sub-GHz set in Signal Workbench. The first
  three measured values train bounded hypotheses; the fourth is held out for
  validation. Report bit range, signedness, byte order, scale/offset and held-out
  result. No automatic protocol generation, TX, key recovery or claim of certainty.
- File History is opt-in. Once enabled, supported native Sub-GHz, Infrared and NFC
  save paths must snapshot an existing user file before destructive replacement.
  Missing history module / SD errors must fail the write, not silently bypass it.
  New files and unsupported types do not need a backup. Third-party writers are
  outside this contract. Recovery creates a new file, never overwrites the current
  one. Keep three recent versions per source, with a fourth staging slot, a size
  limit, checksums and explicit capacity/errors. Backups can contain private data.
- Prefer FAP/FAL implementation and bounded memory. Public API additions are not
  assumed. No region changes, radio transmission or scheduled/upstream-monitoring changes.

## Surfaces, state and data

Cards: list -> create/open -> edit metadata/link files -> save -> inspect/open link.
Card persistence uses checked versioned records; failed writes cannot invalidate
the previous committed card. Deleting a link does not delete its source file.

Sensor Helper: complete compatible RAW set -> enter four measurements in tenths
of a user-chosen unit -> evaluate -> inspect hypotheses -> export a new report.
Changing samples invalidates prior measurements/results. Unknown/truncated input,
equal training measurements and unsupported encodings cannot produce confidence.

History: disabled -> explicitly enabled -> snapshot existing source -> permit
editor write only after validated backup -> inspect versions -> restore a copy.
Partial/invalid records are not offered as usable versions; digest is checked on
restore. Retention removes only owned history slots, never original user files.

## Verification / handoff

Implement with native regression tests for parsing, fitting, held-out failures,
rotation and failed I/O; build firmware/FAPs; compare flash/RAM costs; render all
new screens and error states. Real-device acceptance remains separate. Package
routing and compatibility must be verified before publishing firmware/FW Packages.

## Implemented candidate

### Device Library 1.0 (FW Packages / base)

- `apps/Tools/device_library.fap` and its private
  `apps_data/device_library/plugins/file_history.fal` are package-only targets.
  Neither is to be bundled into the updater resource archive.
- Cards use checked, rotating on-SD records. Notes/tags, eight links and a manual
  checked-date are editable. Removing a link does not remove its file; dirty-card
  navigation asks whether to keep editing or discard the in-memory changes.
- Lists are paginated, 32 entries per page. Names are shown as normal file/device
  names, not hash directory identifiers. Long names fit in menu rows; full paths
  are available in details. The manual checked-date is not an automated test result.
- Explicit SUB/IR links open standard file screens. NFC links use a new
  `inspect:` route into the saved menu, never immediate emulation. Ordinary NFC
  launch behavior is unchanged. Other linked formats remain references only.

### Opt-in history

- Before replacing existing `.sub`, `.ir` or `.nfc` files, the controlled native
  save paths require a successful history checkpoint when enabled. RAW Edit
  already writes new files with CREATE_NEW, so its original-file policy is unchanged.
- The engine copies in bounded chunks and verifies SHA-256 before committing its
  metadata. Corrupt/missing backup payloads cannot satisfy duplicate-snapshot checks
  or restore. Retention keeps three recent versions with a fourth staging/recovery
  slot. A cleanup failure is an error; the extra recovery slot may remain intact.
- Maximum source size is 2 MiB. History status/read/size/module errors cannot
  silently authorize a replacement. A Sub-GHz short write no longer reports success
  merely because a partial destination file exists.
- Restore verifies the backup and resulting copy, uses CREATE_NEW with a unique
  `_restored_NN` suffix, and does not overwrite the current source. It works even
  when the source file is gone, provided its parent folder remains available.
  Excessively long output paths, missing folders or a full destination namespace
  fail explicitly. This is not a backup for SD-card failure: copies share that SD.
- Public API remains 88.13. Automatic history additionally requires the live
  `file_history_caps` firmware record (capability schema 1 plus matching engine ABI), so package-first installation
  or firmware downgrade cannot misleadingly advertise automatic protection.
  Private engine ABI is 2. Existing manual snapshots/cards remain usable without
  the automatic-history capability. Install matching native app resources too.
- History metadata is an integrity/compatibility mechanism, not authentication
  against a malicious FAP or someone modifying both metadata and payload on the SD.
  Checkpoints belong to the supported serialized editor flows, not a global
  filesystem journal for arbitrary concurrent writers or direct RPC writes.

### Sensor Helper (TumoSpectrum 3.1)

- In a complete four-capture Sub-GHz set, open Capture Set Actions > Sensor Helper.
  Enter four measurements in tenths: 24.5 becomes 245. The first three must differ.
  Measurements/results are session-local; changing them invalidates the previous
  report. Export creates a new text file under Signal Workbench reports.
- Reuses the existing pulse-bit inference. Captures must have the same frequency,
  preset and frame length, no parser truncation, and at most 96 bits per frame.
  The reference capture determines the timing profile; the held-out measurement
  never tunes field selection or ranking.
- Enumerates 4–16-bit fields, signed/unsigned interpretations, byte order for
  aligned 16-bit fields, and scales 0.1/1/10 plus offset. Shows up to 16 ranked
  hypotheses and reports the total count in the full report. A separate list opens
  each hypothesis directly, without scrolling through capture paths. Each shows
  MATCH/FAILED for the fourth measurement before the formula, in normal units.
- These are hypotheses, not proof of protocol semantics. Equal/unknown data,
  unsupported encodings or inconsistent measurements cannot produce a success
  claim. No new protocol is installed, no signal is transmitted and no key search
  is added. Hardware sensor measurements remain a separate acceptance step.

## Before publishing

Review the new native GUI previews, rerun the host suite and firmware/FAP build,
verify package-only exports and the updated native NFC/IR resources, then assign
the next firmware/FW Packages versions. Do not mark physical-device, real-SD power
loss, or real-sensor acceptance passed based on host fixtures.

## Local verification (2026-09-22)

- 1,006 host tests passed. Native fault-injection tests use real temporary files
  and the repository SHA-256 implementation: partial writes, failed sync, missing
  and corrupt backups, generation limits, paging and restore-as-copy are exercised.
- Model/storage coverage (not GUI/RTOS/hardware): 98.4–100% lines,
  80.2–92.9% branches, 100% functions in the measured files.
- F7 firmware, Device Library, history FAL, TumoSpectrum 3.1, native NFC and IR
  FAPs built. Public API remains 88.13; Sub-GHz feature import checks pass.
- C1 end `0x080D2E78`: +1,032 bytes versus published 009-008, including the already
  approved Work Profiles / Decoder References work. Section gap 16,776 bytes;
  physical C2 reserve remains 16,384 bytes. No functionality was removed to fit.
- Mapped code/data: Device Library 11,028 B; history FAL 6,100 B; TumoSpectrum
  62,900 B (+4,476 B versus its unchanged 3.0 build). Heap/stack allocations are
  additional; physical peak RAM is not measured.
- 27 native GUI fixtures reviewed, including long names, errors, old-firmware
  capability state, value entry and held-out MATCH/FAILED. These are not photographs
  or physical-button timing tests. New screens still need the user's visual approval.
- Candidate remains local; no push, PR, release or user-device installation.
