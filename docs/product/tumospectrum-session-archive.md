# TumoSpectrum Session Archive — capability contract

**Status:** design and UI preview only; no firmware implementation has been approved.

## CAPABILITY

Let a Flipper owner browse and reopen prior TumoSpectrum capture-set analyses instead of only the most recent set. The archive makes already-saved analysis records discoverable and lets the user see whether their referenced source captures still match the bytes used for the original analysis.

## CONSTRAINTS

- TumoSpectrum already writes timestamp-named `.tsp` capture sets and JSON reports, plus a rolling `latest` view. This capability adds an in-app archive browser; it must not reimplement capture, inference, or decoding.
- Source `.sub` / `.ir` captures are evidence and remain read-only. Do not copy, rename, delete, or transmit them as part of browsing or validation.
- An archived analysis remains viewable even when its source capture is missing or changed. Such a record is a saved result, not a fresh verification.
- Legacy sets without source fingerprints must display `Unverified`; never infer that they are unchanged from matching paths or file sizes.
- A new or updated set records source path, byte length, and a content digest. The digest is an accidental-change detector, not authentication against SD modification.
- No automatic baseline replacement, retention pruning, or cleanup of user files. New exports use create-new semantics and never overwrite existing reports.
- Archive scanning and validation are bounded, cancellable, and do not load an unbounded number of capture payloads into RAM.
- Keep Standard Sub-GHz and Read RAW settings independent; the archive does not apply or modify either profile.

## IMPLEMENTATION CONTRACT

### Actors and surface

- Actor: the Flipper owner reviewing their own saved signal experiments.
- Surface: `TumoSpectrum → Capture Sets → Browse archive`.
- Preserve quick access to the latest set as the first entry under `Capture Sets`.

### States and transitions

1. `Capture Sets` → `Latest set` or `Browse archive`.
2. Archive list → open a saved set by its device/control label and timestamp.
3. Details → `Open saved analysis`, `Check source files`, or `Export report`.
4. Source check → `Unchanged`, `Changed`, `Missing`, `Unreadable`, or legacy `Unverified` per input.
5. On mismatch, keep the saved analysis readable; disable re-analysis until the owner reviews the listed inputs and explicitly starts a new analysis.
6. Back/cancel returns to the prior menu without modifying files.

### UI direction

- Keep navigation in the stock Flipper submenu style. Do not make every state a
  TextBox page.
- Use a compact custom Canvas summary for a saved set: protocol/type badge,
  device/control label, frequency/preset, and a small visual input-integrity
  indicator. Keep the existing TumoSpectrum button affordances.
- Show the four source states as short rows with right-aligned status labels
  (`OK`, `CHG`, `MISS`). Avoid custom-drawn checkmarks, crosses,
  decorative row separators, and improvised icons. Use concise native text for
  legacy or malformed records; put long explanations under About/help only.
- Keep monochrome contrast, pixel fonts, tight spacing, and 128x64-safe labels.

### Data and storage

- Continue reading existing capture-set schema versions. A legacy entry remains visible even when its original inputs cannot be verified.
- Proposed next schema is additive and stores creation time plus a fingerprint per referenced input (path, size, digest). Validate every count, path length, and schema field before display or re-analysis.
- Build the archive index incrementally from the existing capture-set directory; sort newest first and paginate. A malformed entry is shown as unavailable rather than silently skipped or treated as a valid set.
- Preserve the existing timestamped `.tsp` and JSON report as the authoritative snapshot. Any new report is a separate file created atomically with a unique name.

## NON-GOALS

- New RF/NFC protocol support, capture acquisition, protocol inference, secret recovery, replay, or transmission.
- Replacing TumoSpectrum, Capture Inspector, Tumo Acceptance, Work Profiles, or Device Library.
- Cloud synchronization, background monitoring, automatic pruning, or SD-card backup claims.
- Treating a digest match or `Static-like` classification as proof of protocol meaning or transmit safety.

## OPEN QUESTIONS

- Confirm the proposed menu label `Capture Sets` and whether opening an archived record should show the saved report first or the `.tsp` analysis summary first.
- Confirm whether source fingerprints use the existing firmware SHA-256 implementation or another already-exported API; do not introduce a new public API without a measured need.
- Choose a practical archive page size after testing the on-device scan cost on a large SD directory. No automatic retention limit is proposed.

## HANDOFF

Current status is ready for owner review of the full native-display preview. After approval, implement the bounded archive/index and schema upgrade with host tests for legacy parsing, corrupt entries, same-path changed bytes, missing files, cancellation, collisions, and failed writes. Then render all states, build the F7 firmware/TumoSpectrum FAP, measure flash/RAM impact, and run hardware acceptance. This design does not authorize publication.
