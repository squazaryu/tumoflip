# Tumoflip Stable 010 (v1.0.10)

Promotes the reviewed Dev009 line through009-039 and selectively adapts the
remaining Unleashed095 reliability changes. This is Tumoflip's own release,
not an upstream API88.26 parity claim.

Candidate009 was not published: full Linux discovery exposed host-harness
portability errors before upload. Its consumedv1.0.9 tag is retained unchanged.
This release fixes the harnesses without disabling warnings/tests and validates
the full Linux suite in PR CI as well as the release workflow.

## New since Stable008

- Sub-GHz: retain the Dev009 decoder/timing, external-radio, RAW-worker and
  hopping/profile fixes; add bounded Proteco MOVER capture/save/load/playback as
  an external Core module. Preserve captured button bits; no unverified remapping.
- IR: universal databases skip valid unrequested buttons, malformed/empty RAW
  entries cannot reuse the previous signal, and CLI failure teardown is safe.
- Storage/UI: failed folder scans terminate loading and keep parent navigation.
  Case-only file renaming works; incomplete copies/failed destination close do
  not authorize deleting a valid source. Failed deletion/rename keeps favorites.
- Standard and ARF save from the correct generated/history source and do not
  display successful saving after a failed write. Manual-generation selection
  survives saving; stale private feature modules are rejected.
- Retain Dev009's passive NFC/parser hardening, BLE Remote peer fixes, bounded
  optional memory/crash diagnostics, field wallet/rounds/inventory, signal tools,
  module apps and protected FW Packages architecture. No existing functions are
  removed solely to make the firmware smaller.

## Compatibility and migration

**Firmware API advances88.7 →88.23.** Install matching native resources and
FW Packages; do not reuse arbitrary old feature FALs. The private Sub-GHz
feature-module contract is1004. Public API numbers are local to Tumoflip.

1. Back up `/int`, important SD captures, settings and application data.
2. In TumoCompanion choose **Firmware → Stable →010**, including native resources.
3. Then install the matching independent **FW Packages Stable007** after its
   separate catalog activation. Older Stable006 is a rollback catalog, not the
   new snapshot. TumoCompanion1.11.35 supports this workflow; no new IPA is required.
4. No factory reset, automatic SD cleanup, Bluetooth re-pairing or ESP32-board
   flash is required. Preserve original files before trying any rename/error test.

Old Device Library cards are read without automatic rewriting; explicitly saved
newer card formats may require the matching newer app on downgrade. Backup
manifests are not substitutes for off-device copies of capture bytes.

TM01x irreversible finalization, new credential/seed-capture workflows, receiver
resync packs, Hiland keystore replacement,112-bit certificate TX and the new
Remote Analyzer are not silently enabled by this promotion. Existing deferred
decisions and protected Community audit acceptance are unchanged.

## Verification boundary

Publication is gated on full host tests, normal and diagnostic F7 builds,
CodeQL, exact updater-flashed DfuSe ranges, the full4096-byte C2 reserve,
native FAP/FAL/import/archive validation and public downloaded-byte checks.
Selected native production bodies run under fatal ASan/UBSan; these are not
whole-firmware/RF certification or evidence of historic OOM/BLE recovery.

Fresh independent review and exhaustive physical acceptance were not performed.
Use owned devices/copies to check Proteco/CAME, IR database errors, missing-SD
navigation, Standard/ARF saving, old cards/settings, BLE and repeated launch/exit.
No attached device was flashed by the publication task.
