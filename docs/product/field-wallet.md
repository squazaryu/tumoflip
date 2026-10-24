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

## Implemented behavior / installation

Device Library 1.1 remains a protected FW Package. Install the matching firmware
and native resources first, then update FW Packages. No iOS or ESP32 change is
required. Native RFID/iButton resources advertise version 1.1 and are checked for
F7, compatible running API, exact application name and inspect support before
opening them. Missing/older drivers fail closed; they are never opened through
their legacy automatic-emulation route.

Open Tools > Device Library. Create an object or open an existing card, then use
Edit object > Add file action. Select a linked row in Edit object to rename it;
an empty label uses its original filename. Save changes before opening actions
or pinning. Pinned objects are a separate bounded quick-access list (16 entries).
Remove a pin only removes the shortcut, never the object or its capture files.
Known file kinds are SUB, IR, NFC, RFID and iButton; scripts/FAPs/unknown types
remain reference-only. No file is automatically transmitted or emulated.

## Data / downgrade boundary

Legacy TDC1 cards are read in memory without rewriting their files. Explicit
Save writes TDC2 cards with eight optional 31-byte action labels, retaining four
checked rotating revisions. A successful save requires sync, close and matching
readback generation plus SHA256, not merely another structurally valid record.
Pins use separate checked rotating records under the owned pins directory.
I/O failures retain the previous valid revision; this is not a global concurrent
filesystem transaction or a backup against failure of the SD card itself.

Device Library 1.0 cannot read new TDC2 revisions. Do not downgrade that package
after editing cards without retaining/restoring your own card-data backup. The
firmware update and removing links/pins do not delete SUB/NFC/RFID/IR/iButton files.

## Nice / release checks

Selective adaptation of Unleashed 6dcf8b822b2cfea6ad790d91aec4a1e39860c18e:
plain O-Code counters at the existing 52/72-bit cipher boundaries, shared by core
and Nice O-Code 1.4. Existing saved Key bytes are read unchanged. Literal counter
modes, overrides, wrap, regular Nice FloR-S/One and explicit IC semantics remain.
Private code sharing and removal of redundant output initializations preserve
the full C2 erase-page reserve without deleting functionality or exports. The
binary API stays 88.22; the small conversion helper is source-only in the SDK.

Native test modules: test_nice_counter, test_field_wallet, test_wallet_inspect,
test_wallet_menu and test_wallet_flow, in addition to the existing Nice, history,
metadata and full firmware regressions. Previews use production constructors,
draw callbacks and fonts; long labels, empty lists, scrolling, errors and unsaved
navigation are included. Publication is separate from real-device acceptance.

Manual acceptance after installation: open an old card; name/save actions;
pin/unpin and reopen after restarting the FAP; open owned NFC/RFID/iButton files
into their saved menus without automatic emulation; exercise missing-file and
missing-driver errors; confirm deleting a link/pin leaves the capture intact.
Test existing Nice captures only with your own compatible receiver.
