# Upstream decisions — 2026-10-01

Comparison baseline: Tumoflip `dev` at `dfa1bbbe09ef113752a07f2956b125c3b2f9e726`
(Dev 009-015), Unleashed `dev` at `a3d157e0eba345639d986470ca344b917c056e3e`,
and Community Pack `30sep2026p2` from source `09a9587b481a93dbfe022382a0c0790f0e8aca94`.
This is a decision record, not a release or a claim of hardware acceptance.

## Selected adaptations

- Unleashed `9af1a6d807`: bounded nested NDEF SmartPoster parsing, reject an
  unknown saved iButton protocol, bound DESFire access-right bytes, and reject
  negative or partial TAR reads. FeliCa block counts and ISO14443-3A cascade
  handling are already covered on Tumoflip `dev`.
- The same commit's RPC app-context hardening is isolated in a separate patch.
  The existing Tumoflip loader-status and App Bridge behavior must be retained.
- Specter is a package-only FAP. Selectively adapt the canonical meter scale,
  v2 settings migration and skippable intro from original-author tag `v3.1.1`
  (`079474ba10c54fc7f9e23d4f896a2982dda0a64e`), retaining Tumoflip's
  logging guarantees. Show native UI renders before any package publication.

## Deferred or intentionally not imported

- Unleashed `08afbae1b2` moves `LFRFIDProtocolIndala224` to the end of the
  publicly exported enum. Tumoflip already shipped the earlier positions at
  API 88.14. Reordering without an API bump would silently reinterpret numeric
  protocol IDs in installed FAPs. Coordinate a future API bump, rebuild FW
  Packages and check saved RFID data before changing the order.
- Unleashed `eedbad331b` adds the legacy `mf_desfire_send_chunks` exported
  symbol and raises API to 88.15. Current Community ABI findings do not require
  that symbol. Do not bump just for parity; review with the next paired package
  API change.
- Unleashed `e4eda9efca` adds Prastel manual generation/button mapping whose
  own comment says receiver behavior is unverified. No TX adaptation until an
  owned-device capture and receiver test exist. `64d285c8e2` Seed Capturer is
  not needed for the agreed passive decode/analysis workflow.
- ProtoPirate 3.7 in Community Pack is a broad plugin-host/navigation rewrite,
  not a direct Standard Sub-GHz update. The new source passes a 20-byte bound
  to a 16-byte frequency buffer and contains an unused helper copying bytes
  into a `FuriString*`; review those boundaries and the existing Standard
  integration before selecting any safe subset. Do not import brute-force or
  secret-recovery actions under this decision.
- Sub Duplicate Finder 1.3 stays in Community Apps. It can delete a selected
  `.sub` file; test on copies of recordings rather than migrating deletion into
  firmware or FW Packages.

The protected-app audit for Community Pack `30sep2026p2` remains open in
`squazaryu/tumoflip-fw-packages#143`. No automation prompt, protected audit
disposition, catalog release or user file was changed by this decision.
