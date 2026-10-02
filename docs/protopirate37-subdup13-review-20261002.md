# ProtoPirate 3.7 and Sub Duplicate Finder 1.3 review — 2026-10-02

This is a source/host-test decision, not a firmware or package release and not
physical-device acceptance. No user SD files were read or changed.

## Exact inputs

- Tumoflip `dev`: `42aad5d7e16cdb430fd7bbed1c37cf5164b3a334` (Dev
  009-016 plus the Specter-only source merge).
- Community Pack `30sep2026p2`: `09a9587b481a93dbfe022382a0c0790f0e8aca94`,
  `base_pack/protopirate` version 3.7. Its earlier 3.2 source snapshot is
  `31cbcadd9ecdc076c8151cce2e38be4e66335f55`; the 3.2-to-3.7 subtree
  changes 80 files (+6909/-3780).
- Community Pack `1oct2026p3`: `3e6a9ca199cdb4664c1998952deedc8533c420d5`,
  ProtoPirate 3.8. This supersedes 3.7 as the current Pack snapshot.
- [Sub Duplicate Finder v1.3.0](https://github.com/Endika/flipper-sub-dup/releases/tag/v1.3.0):
  `29afd02a650db9cd284962a0fec6e21588de8661`.

## ProtoPirate decision

Tumoflip Standard Sub-GHz already has on-demand protocol packs, automatic RAW
decoding across packs, progress/errors, and restoration of the original pack on
exit. The separate ProtoPirate FAP retains its own receiver and file workflows.
Importing the 3.7 plugin-host/navigation rewrite into Standard would duplicate
those paths and replace Tumoflip's guarded plugin loading. Version 3.7 also
adds Renault V1/Hitag2 together with key-recovery and brute-force code; this
is not an isolated passive decoder that can be accepted as-is. Mitsubishi V0
and Porsche support remain in Tumoflip's Standard packs even though 3.7 removes
their standalone ProtoPirate implementations.

Direct import is blocked by concrete 3.7 source defects:

- `protopirate_scene_sub_decode.c` formats a frequency with `snprintf(..., 20,
  ...)` into `char frequency_str[16]`.
- `protopirate_protocol_plugin_host.c` ignores a failed `shared_plugin_load`
  result and then dereferences the plugin pointer.
- `protopirate_txrx.c` copies character bytes with `memcpy` into a
  `FuriString*`, not through the string API. It appears unused in current call
  sites, but remains unsafe to reuse.

The 3.8 snapshot fixes the first two call sites, but still has the `FuriString`
copy. Tumoflip's existing corresponding paths use buffer-sized formatting,
check plugin load status, and call `furi_string_set_str`. No API bump or direct
ProtoPirate import is justified. The 3.8 Fiat V2 save-field normalization is
a narrow follow-up candidate from the inspected delta; it needs synthetic and
owned-capture save/reopen regression tests before any source edit. Renault V1 needs separate
passive-only extraction and capture validation, without recovery actions.

## Sub Duplicate Finder decision

Keep v1.3.0 in Community Apps. Its host `make test` suite passed. The app scans
one folder and groups files solely by identical byte length and CRC32. The
Delete confirmation then calls `storage_simply_remove` for the selected path
without comparing file bytes or rechecking the file after scanning.

A host probe compiled the actual v1.3.0 `logic.c` and used three temporary
`.sub` copies: one genuine duplicate plus a *different* 146-byte payload with
the same CRC32 (`0d869890`). `process_duplicates` put all three in a single
group. The different payloads had different SHA-256 digests. No file was
deleted. This proves that a displayed duplicate group is not sufficient
evidence for safe deletion, even though CRC32 collisions are uncommon.

Until the author adds byte-for-byte confirmation and a pre-delete identity
check (ideally with a recoverable move/backup), do not use Delete on original
recordings. Any device test must use a dedicated folder of disposable copies;
the host probe does **not** count as a successful Flipper SD deletion test.

Prastel TX buttons, Seed Capturer, API migration, Community disposition and
scheduled automation are unchanged by this review.
