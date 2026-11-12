# Unleashed095 review and Stable009 preparation — 12 November 2026

Source boundary: [unlshd-095](https://github.com/DarkFlippers/unleashed-firmware/releases/tag/unlshd-095),
published2026-11-11T17:15:37Z, exact tree9c6327764c18fc8376b5941aa8bde79fe681fe8f.
Compared with our last reviewed upstream b18296933c and published Tumoflip
Dev009-039 /44561c220a. New upstream Dev e43be5aff7 changes issue templates only.
Own Stable remainsv1.0.8/e2cf3ba8 before this work; main is an ancestor of dev
(0/646). The dirty primary checkout is untouched; reuse the clean isolated tree.

## Selected adaptations

- **IR3433331e31:** a universal database may hold valid names the caller did not
  request. Validate all records but collect only requested offsets; preserve an
  explicit not-found result when none matched. Reject a zero-timing RAW entry
  before it can reuse the previously parsed signal. A vanished database must not
  be stopped twice by the CLI. Reset discards a borrowed filename; an unconfigured
  calculation returns an error instead of dereferencing NULL. No new IR error
  enum/public export is required. Existing defensive parsing remains.
- **Browserb710cdcb17:** report open/count/read failures, distinguish EOF from
  SD errors and finish callbacks even after offset shrink/abort. Drop stale rows
  and loading flags, retain a real parent entry and show Cannot read folder.
  Native FileBrowser uses private deferred allocation so every callback is set
  before the first scan. The public worker allocator still auto-starts for old
  FAPs; no existing exported signature/behavior is removed or repurposed.
- **Rename3060cee9ba:** allow a case-only basename change on the same filesystem
  through a bounded unused same-directory temporary name. Retain a recovery copy
  if rollback fails and log its path. This is not a power-loss-atomic filesystem
  transaction. Do not skip the copy's full byte count or destination close/flush
  result: an incomplete backup cannot authorize removal of the valid original.
  Validator optional names are explicitly NULL; successful case changes are not
  falsely rejected as another file. Failed Archive renames retain favorites.
- **Add Manually1fe237846c:** separate the remembered menu row from generated
  data ownership, including all four generator paths and ARF. Reset the generated
  flag on every exit, without erasing the selected menu row. Both Standard and
  ARF now refuse to display Save success after a failed actual write. The private
  feature-plugin marker advances1003→1004, so older feature FALs fail closed
  instead of using mismatched save semantics. Public F7 API remains88.23.
- **Proteco57d471868b:** external Core FAL for captured24-bit static MOVER
  signals. Require two matching frames, measured-clock guard bounds, exact key
  width, bounded counters and independent file timing/repeat defaults. Three
  upstream public RAW captures and synthetic rate/key/noise cases are retained
  as regression fixtures. Preserve the captured button bits; do not synthesize
  the upstream-unverified fourth-button mapping. No Add Manually entry is added.

No task writes to a real Flipper or SD. Storage tests operate on an in-memory
filesystem; protocol tests do not transmit. These changes do not establish what
caused historic missing recordings, idle-OOM symptoms or BLE icon disagreement.

## Release095 coverage, not an upstream-parity claim

| Family | Tumoflip result |
| --- | --- |
| HSM7/8 preambles, KeeLoqTE200, Nice52/72 count/metadata, Ansonic/CAME TWEE/buffer bounds | Already selectively adapted and tested in Dev; keep our legacy regressions |
| Freezer, FAAC TM, explicit868MHz, Dooya buttons, FDX-B134, Sub-GHz Remote/RAW completion and Wardriving memory/file preservation | Existing Dev implementations retained; do not import competing radio/worker architectures |
| GUI redraw/BLE-SD icons, FatFs fragmentation, Ultralight page bounds, diagnostic profile/truthful tests and deterministic build gathering | Already covered locally; keep without re-porting or enabling JavaScript |
| IR, browser errors, case rename, Add Manually state and Proteco | Selected source adaptations above |
| Nice112-bit certificate TX/enrollment, receiver-resync packs/Hiland database, new Remote Analyzer, TM01x one-time finalization and T5577 credential tooling | Previously separate/deferred scope, not silently enabled by a Stable promotion |
| Upstream API88.26 | Not our ABI label: its writable/bit-reversal/Remote Analyzer/T5577 exports are not needed for this patch. Keep88.23 and check actual imports |

The author-app/source boundary of Community11nov2026 and unresolved protected
audits are not accepted by this firmware task. No Community binaries, encrypted
vendor keystore replacement, user dictionary, new brute-force path or audit-pin
override is imported. Existing legitimate functions are not removed.

## Stable delivery / validation

Promote the previously approved Dev009 line to a new immutablev1.0.9 /
t-flppr-fw-009 identity, not a replacement for008 or039. Stable008's API88.7
and catalogStable006 are not advertised as the new payload set. Compose a new
independent Stable007 catalog from the exact verified Stable009 firmware
manifest/ZIP, preserve the old rollback release, and activate only after its
public bytes and catalog contract have passed. Dev031 and iOS1.11.35 remain
unchanged; the current iOS client supports firmwareSnapshot catalogs.

Migration: back up /int and important SD captures/data; install Stable009 with
matching native resources first, then Stable007 FW Packages. Private feature
FAL1003 is incompatible with1004. No reset, SD cleanup, automatic signal deletion,
board flash or Bluetooth re-pairing is required. New independent package content
is the matching snapshot, not a protected Community acceptance decision.

TDD checkpoints record actual RED before production changes, then GREEN with
fatal ASan/UBSan. Named production-body coverage is measured separately from
whole-firmware/RF behavior. Full host discovery, FAP/FAL imports, actual updater
DfuSe/4KiB reserve, SDK/native archives, CI/CodeQL and downloaded public bytes
must pass before the final publication report. Exact metrics are recorded in
the task receipts after final source/version builds.

Fresh independent review and physical/RF acceptance are not performed. Manual
checks remain: owned Proteco/CAME recordings, a copied file's case-only rename,
missing-SD navigation, IR database errors, Standard/ARF generated/history saving,
old saved files and matched FW Package installation. Never run deletion/error
tests on original recordings.
