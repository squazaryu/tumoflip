# Field kit follow-up — 2026-10-26

Authorized: implement all selections from the upstream/own-capability review.
Publication, installation and physical-device acceptance are separate checkpoints.
Base: Dev009-031, `4be8c28f981a9f1299b4164675872aaf21dec144`, API88.22.
Reused worktree: `tumoflip-specter-311-adaptation`; branch `feat/field-kit-20261026`.
Temporary build/test outputs: `/tmp/tumo-field-kit-20261026.QNpS58`.

## Work and acceptance

1. Nice O-Code: adapt asymmetric counter maps from Unleashed `c66f01dce7`;
   exhaustive inverse and independent vectors, existing52/72-bit frames,
   literal modes, override/wrap, plain FloR-S/One and old saved-byte preservation.
   Read upstream `No_IC` without weakening our explicit `ICKnown` semantics.
   Do not add112-bit TX, enrollment or new key-search behavior.
2. ARF `9b18419de2`: review new automotive decoders against existing equivalents;
   adapt genuine receive-only differences plus Urmet/X10. Preserve original
   captures, current protocols and IDs; no new automotive key/seed brute force.
   Keep added decoders lazy-loaded, not another duplicate control application.
3. TumoSpectrum: bounded peak history and broker-owned dual-radio sweep, with
   external-loss/error cleanup and existing single-radio fallback. Preserve the
   existing peak-hold, capture-set and profile workflows.
4. Passive NFC Sniffer: separate package-only FAP; document source/license,
   bounded buffers, exclusive resource ownership, cancellation and cleanup.
   No key recovery tooling or active relay; no device capture during development.
5. Field rounds: existing object cards form a bounded checklist; explicit
   pass/problem/skipped result, notes/time, resumable checked storage and new
   report export. Opening an action is never a successful physical check.
6. Portable object: export card plus referenced capture bytes and digests;
   validated bounded import creates separate files/card, never overwrites an
   existing capture. Reject path traversal, malformed/oversized/changed input.
7. Capture inventory: explicit read-only scan and baseline comparison reporting
   missing/changed/unreadable/new entries; never delete or auto-rebaseline.
   Export baseline off the SD; a manifest is not a backup of missing file bytes.

## Gates

- Runtime/intentional compile RED, then GREEN; atomic TDD checkpoints.
- Native ASan/UBSan parser/model/storage and error-injection tests; >=80% measured
  production coverage for newly introduced logic, not mocked substitutes.
- Native128x64 previews for all new menus, long names and errors.
- Full host suite, FAP/FAL/import/API/package checks and exact core-size report.
- Keep4096-byte physical C2 reserve; do not remove features/exports to claim space.
- Do not flash/reboot devices, change BLE pairing, or clean/write the user's SD.
- No unsolicited subagents. Independent agent review is not performed.
- Remove task-created outputs after validation when no longer required; preserve
  source, compact evidence/previews, private crash evidence and user files.

## Progress

- Baseline confirmed clean;1196 existing host tests passed before implementation.
- Implementation in progress. No new candidate, release or hardware result yet.
