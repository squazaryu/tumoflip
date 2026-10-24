# Field wallet — implementation contract

Authorized scope: selectively adapt the Nice O-Code counter correction, then
extend Device Library with practical object actions and prepare the next Dev.
Object rounds/checklists are a separate later feature, not part of this change.

- Nice: plain16-bit press counts at existing52/72-bit cipher boundaries; preserve
  plain Nice One/FloR-S, literal counter modes, saved Key bytes and explicit IC
  semantics. No112-bit TX, enrollment or additional key search. Prefer a shared
  inline/private helper over an unnecessary exported symbol/API bump.
- Wallet: existing eight references become explicitly named actions for NFC,
  LF RFID, iButton, SUB and IR. Keep original files read-only until the normal
  native app's explicit user action. Inspect routes must not emulate on launch.
- Add bounded pinned-object access, not another radio or macro engine. Unknown
  formats remain reference-only; never run scripts or arbitrary FAPs implicitly.
- Read old cards without automatic rewriting. Checked rotating writes must keep
  the last valid version on errors, including new metadata and pin records.
  Removing a link/pin must never delete its original file.
- Prefer FAP-only changes, bounded memory and no public ABI change. Preserve the
  full4096-byte physical C2 reserve; build with the pinned toolchain at-j2.
- Preview all new native screens, including long names/errors/unsaved state,
  before release. Tests/CI/publication are not hardware acceptance.
- Firmware, baseline resources and the independent Device Library FW Package
  must be verified separately. Stable, iOS, ESP32 and the attached devices are
  unchanged unless separately needed/authorized. Do not flash hardware.

Implementation-ready. Pin-count limit, exact metadata representation and native
inspect compatibility checks are engineering choices to verify with tests.
