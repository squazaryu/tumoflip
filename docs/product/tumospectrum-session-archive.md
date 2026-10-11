# TumoSpectrum Session Archive — capability contract

**Status:** merged to `dev` in PR #545; Dev 009-023 release preparation is in progress. Device acceptance remains pending.

## CAPABILITY

Let a Flipper owner browse and reopen prior TumoSpectrum capture-set analyses instead of only the most recent set. The archive makes already-saved analysis records discoverable and lets the user see whether their referenced source captures still match the bytes used for the original analysis.

## CONSTRAINTS

- TumoSpectrum already writes timestamp-named `.tsp` capture sets and JSON reports, plus a rolling `latest` view. This capability adds an in-app archive browser; it must not reimplement capture, inference, or decoding.
- Source `.sub` / `.ir` captures are evidence and remain read-only. Do not copy, rename, delete, or transmit them as part of browsing or validation.
- An archived analysis remains viewable even when its source capture is missing or changed. Such a record is a saved result, not a fresh verification.
- Legacy sets without source fingerprints must display `Unverified`; never infer that they are unchanged from matching paths or file sizes.
- A new or updated set records source path, byte length, and a content digest. The digest is an accidental-change detector, not authentication against SD modification.
- No automatic baseline replacement, retention pruning, or cleanup of user files. New exports use create-new semantics and never overwrite existing reports.
- Archive browsing uses the stock file browser. Source hashing is bounded by the existing 1 MiB parser limit per file and runs in a worker; Back cancels during verification and before the archive write begins.
- Keep Standard Sub-GHz and Read RAW settings independent; the archive does not apply or modify either profile.

## IMPLEMENTATION CONTRACT

### Actors and surface

- Actor: the Flipper owner reviewing their own saved signal experiments.
- Surface: `TumoSpectrum → Capture Sets → Browse archive`.
- Preserve quick access to the latest set as the first entry under `Capture Sets`.

### States and transitions

1. `Capture Sets` → `Latest set` or `Browse archive`.
2. Stock archive browser → open a saved set using the sanitized device/control/date filename.
3. Opening a set shows its saved analysis. `More` offers `Check source files`,
   `Export report`, and the existing profile/Companion actions.
4. Source check → `Unchanged`, `Changed`, `Missing`, `Unreadable`, or legacy `Unverified` per input.
5. On mismatch, keep the saved analysis readable and report the changed/missing inputs. A new analysis is always a separate capture set; the saved snapshot is never replaced.
6. Back/cancel returns to the prior menu without modifying files.

### UI direction

- Keep navigation in the stock Flipper submenu style. Do not make every state a
  TextBox page.
- Use the existing compact Capture Set Canvas view for a saved set: type badge,
  device/control label, source-status summary, and existing TumoSpectrum buttons.
- Show the four source states as short rows with right-aligned status labels
  (`OK`, `CHG`, `MISS`). Avoid custom-drawn checkmarks, crosses,
  decorative row separators, and improvised icons. Use concise native text for
  legacy or malformed records; put long explanations under About/help only.
- Keep monochrome contrast, pixel fonts, tight spacing, and 128x64-safe labels.

### Data and storage

- Continue reading capture-set schemas 1 and 2. Legacy entries remain visible as `Unverified` (or `Missing` if a source path is gone).
- Schema 3 stores creation time plus a SHA-256 and byte length for each source. The digest is an accidental-change detector, not authentication.
- Browse `.tsp` files in the existing capture-set directory with the stock Flipper file browser. New names begin with sanitized device/control labels and include capture time; existing timestamp-only files remain visible. A malformed entry is reported as unavailable rather than accepted as a set.
- Timestamped `.tsp` and JSON files remain the saved snapshot. Exports use create-new names; the rolling `latest.tsp` remains for existing launcher/Companion flows.

## NON-GOALS

- New RF/NFC protocol support, capture acquisition, protocol inference, secret recovery, replay, or transmission.
- Replacing TumoSpectrum, Capture Inspector, Tumo Acceptance, Work Profiles, or Device Library.
- Cloud synchronization, background monitoring, automatic pruning, or SD-card backup claims.
- Treating a digest match or `Static-like` classification as proof of protocol meaning or transmit safety.

## HANDOFF

Host tests, the TumoSpectrum FAP build, the PR clean F7 build, and Build & Analyze pass. The exact Dev 009-023 updater/package validation, on-device file-browser/layout review, SD failure injection, and physical-device acceptance remain outstanding. The release is a Dev candidate; it must not be promoted to stable until the hardware checklist is completed.
