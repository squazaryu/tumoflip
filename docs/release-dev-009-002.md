# Dev 009-002: RFID formats and file-only capture conversion

## Scope

- RFID Add Manually offers numeric FC/ID input as well as raw hex input.
- App-side HID H10302/H10304/H10306, AMAG S10401, Corporate 1000 35-bit and Casi-Rusco C10106 interpretations supplement the existing protocol views.
- Empty NumberInput submits zero, clamped to the field's allowed range.
- ARF hub → **Capture converter** opens a package-only utility for existing captures.
- API stays **88.9**. The 009-001 protocol inventory and existing functions are retained; this release adds no new radio encoder or secret-recovery module.

## Capture converter

Install the release's `tumoflip-packages.zip` ARF group via FW Packages before opening the new menu entry. The converter is deliberately not bundled into updater resources.

- **PSF → SUB** scans `/ext/apps_data/proto_pirate/saved` recursively and copies to `/ext/subghz/imported`.
- **SUB → PSF** scans `/ext/subghz` recursively and copies to `/ext/apps_data/proto_pirate/saved`.
- Both formats use the canonical `Flipper SubGhz Key File`, version 1 header. Every byte is preserved, including the protocol name, custom-preset data, comments and unknown metadata. Select the original protocol pack to read the result; conversion does not add protocol support.
- Only key captures with Frequency, canonical Preset, Protocol and Key fields are accepted. RAW-recording headers, malformed/binary files, files over 1 MiB or lines over 1024 bytes are skipped.
- Existing destinations are not overwritten. Name conflicts receive a bounded numeric suffix. Back requests cancellation during scanning, copying and verification.
- Success requires full write, storage sync, successful close and complete byte-for-byte readback. A failed/cancelled output is removed when the SD remains accessible. Originals are opened read-only.
- This is not a power-loss transaction: sudden power loss or removal of the SD may leave an incomplete new output. Originals and pre-existing files are never replaced.

## Hardware acceptance (pending, not implied by host tests or CI)

- [ ] Install 009-002; confirm version and API 88.9, boot and BLE connection.
- [ ] RFID → Add Manually: test numeric and hex paths; save/reopen a test file, then cancel each input screen. Check zero/max values and a 37-bit card number above 32 bits.
- [ ] Install the matching ARF package group; open Capture converter from ARF hub, return Back to the same hub.
- [ ] Use copies of test captures. Convert both directions; confirm original and output contents match and the appropriate protocol pack opens the copy.
- [ ] Repeat conversion with a name collision; check existing files are unchanged and a new suffixed file appears.
- [ ] Cancel while scanning and while copying; confirm responsive navigation, correct summary and successful subsequent run.
- [ ] Confirm malformed and RAW files are reported as skipped rather than copied successfully.
- [ ] Smoke-test NFC, SD browsing, FAP loading, Standard/Read RAW independent settings and Companion.

No firmware or package is automatically installed on the attached device by the release process.
