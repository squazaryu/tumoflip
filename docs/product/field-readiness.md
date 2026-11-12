# Proposed field follow-up — 7 November 2026

Status: proposals for owner selection, not implemented features or a release
promise. The current request authorizes considering original work alongside the
bounded upstream adaptation, not silently adding a new large product workflow.

## Existing capabilities are the starting point

Device Library already has named file actions, pins and opt-in history. Field
Tools already supplies explicit object rounds, resumable results/notes, portable
object bundles and a read-only capture inventory/baseline comparison. Sensor
Watch, Power Journal, UHF Inventory and Varta Inspector already produce reports.
Tumoflip Packages already displays read-only package SHA/legacy-path diagnostics.
TumoCompanion already views the four sensor/power/UHF/Varta CSV schemas.
Do not sell a second copy of these as a new feature.

## 1. Per-object field readiness in Field Tools

Before a round, show one bounded readiness card for the selected object:
linked captures readable, required native inspect driver present/compatible,
required Module One/radio selected and available, and capture changes relative
to an explicitly chosen inventory baseline. Open the existing detail screen for
each failed check. Never start RF transmission, emulation or an external script
as a preflight test; presence/compatibility is not proof that a receiver works.

Prefer existing native Flipper icons, stock menus and short status rows rather
than a dense text wall. Results: ready/attention/not checked, with the actual
reason and a bounded list. Missing metadata is unknown, not success. No automatic
rebaseline, journal repair, file deletion or firmware-core/API expansion.

Acceptance: missing/changed/unreadable files, old/missing driver, disconnected
external module, canceled scan, low/fragmented heap, long object names and empty
cards. Confirm original capture hashes and existing rounds remain unchanged.
This is the first recommended implementation, extending the existing FAP.

## 2. Read-only object-round reports in TumoCompanion

Add an adapter/view for existing Field Tools round exports: object, step,
manual pass/problem/skipped result, notes/time and capture references. Preserve
unknown fields and show incomplete rounds as incomplete. A user-marked pass is
not automatically a measured hardware pass. Missing referenced files remain
visible; opening a report cannot rename/delete captures or rewrite the round.

Keep this separate from the existing four CSV report formats. Start with
bounded local import/read-only viewing; no new background BLE polling, remote
actions, automatic uploads or writeback protocol. Native-format fixtures and
iPhone previews, including errors/long text, are required before publication.

## 3. Extend existing package diagnostics, not another installer

The native Packages FAP could explain the selected dual-slot journal generation,
pending transaction phase, available rollback evidence and why a recovery is
blocked. The current SHA/legacy audit alone does not explain these states.
First align its read-only parser with the Companion journal contract, including
invalid CRC/generation, incomplete slots and untrusted paths. Never clear a
journal or delete staging files merely because an audit cannot parse them.

This is secondary to field readiness and round reports: useful maintenance,
not a substitute for a new field-use capability. No implementation or physical
acceptance of these proposals is claimed by the November7 upstream patch.
