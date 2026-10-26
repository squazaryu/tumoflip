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
- Nice maps: RED89ba604976 -> GREENc7e19eea38; No_IC RED7168c77a25 ->
  GREEN9ff4417b42.25 native Nice/094 tests passed. Existing52/72-bit-only TX
  scope retained; old saved Key bytes remain unchanged.
- Own engines/menu: field_round, field_bundle, field_inventory, field_menu and
  package-only field_tools FAP implemented. Fault/cancel/collision tests and
  content-identity checks now pass; actual package routing and F7 imports are
  verified. Old cards and original captures are retained.
- Spectrum: broker-owned dual sweep and16-entry peak history implemented;
  native hardware-stub/stock-menu tests (22 including existing workbench tests)
  and actual F7 signal_workbench build/APPCHK passed. Peak selection uses the
  existing Band Map. Long Right opens peaks; Down cycles INT/EXT/dual.
-21 Field Tools and20 radio/NFC frames rendered with production callbacks and
  stock fonts, including input, errors and scrolling. Durable sheets:
  [Field Tools](../assets/field-kit-2026-10/field-tools.png) and
  [Passive NFC / Band Map](../assets/field-kit-2026-10/field-radio.png).
  These are not physical LCD or RTOS input acceptance.
- First core-only build after Nice passed (`firmware_all`, toolchain39, -j2).
  Text698148+rodata177384, data736, BSS7616. This is an intermediate build at
  the old version identity, not a release or a final C2 validation.
- ARF decoder integration, passive NFC capture, package routing, expanded
  fault/fuzz/coverage tests, previews and exact size/DFU gates are now complete
  locally, within the verification boundary below. No release or hardware action
  has occurred.

### Intermediate ARF/core checkpoint

- Compact resident protocol filenames restored the erase-page reserve without
  removing any protocol: C1 end0x080D5F30/section4304/physical4096 before adding
  the new entries. After routing ten new receive-only FALs, the intermediate
  build is C1 end0x080D5FC8/section4152/physical4096 at unchanged API88.22.
- Ten new protocol leaves compile for F7 without unresolved imports. Actual
  production decoders executed2,000,000 deterministic malformed pulse events
  under hard-failing ASan/UBSan. This is not a physical-protocol certification.
- Toyota V1 and Mazda V1 are not duplicate plugins; the comparison and source
  boundary are recorded in `applications_user/field_rx_protocols/PROVENANCE.md`.
- Long serializers exposed an upstream Key2 omission. The initial standalone
  test needed fixture-linkage correction; valid runtime RED was then replayed
  on exact imported source701a7f5aa9. Explicit checked Key2 writing/loading is
  implemented and the focused suite is green. X10 and Urmet additionally pass
  synthetic pulse -> accepted frame -> save -> load roundtrips, including full
  Urmet Raw/Gaps. Physical captures for all protocols remain pending.
- Full intermediate host discovery passed1208 tests before the NFC ownership
  API addition. For NFC capture, new nonfatal atomic IRQ reservation is needed
  instead of the asserting setter:32 contenders produce one owner; invalid and
  busy reservations fail without replacing it. Added only
  `furi_hal_interrupt_try_set_isr`; local API is88.23, not upstream export parity.
  Intermediate F7 C1 end0x080D5FF0 keeps4096 physical reserve (4112 section gap).

## Final local candidate

- Firmware source/build commit: `390162ac6ba1cf031d833f661e5b1b11f290c4ce`.
  The following evidence/preview commit changes documentation only.
- Identity: **unpublished** `t-dev-009-032`, local F7 API88.23. Only new public
  symbol: `furi_hal_interrupt_try_set_isr`. No upstream API-parity claim.
- FW Packages policy/protection commit:
  `eb60035254b9dbcb1b1d73be16e2b8d5cf2744e9`.
- Companion protection/requirements commit:
  `04ed4a9b8449d6b4897429b770bb91b85d9580b3`.
  No app version, feed or IPA was changed/published. Native production Swift
  protection tests passed; iOS Simulator/XCTest and physical phone acceptance
  were not run for this narrow policy-only patch.
- Versions: Device Library1.2, Field Tools1.0, TumoSpectrum3.4.0,
  experimental Passive NFC1.0, ARF Status0.4.3.
- Builds: installed toolchain39, `-j2`, `COMPACT=1 DEBUG=0`, LTO retained.
  `fw_dist`, `fap_dist`, updater packaging were separate invocations.
- Full firmware host discovery: **1226 passed**. FW Packages: **335 passed**.
  Negative notification tests deliberately print error/needsReview messages;
  they do not clear existing audit findings or authorize publication.
- New ten RX FALs and five affected FAPs: real ELF metadata F7/API88.23;
  every undefined import resolves against the public firmware CSV. Existing
  Frequency Analyzer/Add Manually/Workspaces FALs also resolve their private
  imports against the exact Sub-GHz host table and resident ELF.
- `validate_release.py --min-c2-gap 4096 --write-manifest`: **passed**.
  C1/DfuSe end`0x080D5FF0`; erase-aligned end`0x080D6000`;
  radio starts`0x080D7000`; physical reserve**4096 bytes**, section gap4112.
  The309-byte DFU container overhead is not flash consumption. Updater120209
  bytes is below131072. This is preserved reserve, not newly gained free RAM.
- Final ARM size: resident text875776/data744/BSS12600 bytes (ELF totals).
  FAP text/data/BSS: Field Tools22209/4/0, Passive NFC7955/0/16072,
  Device Library17013/4/0, TumoSpectrum72988/4/0. These exclude dynamic
  allocations, RTOS stacks, display modules and loader overhead.
- Actual package ZIP: Base33, ARF14, Module One44, Protocol Packs42 entries;
  all133 payload SHA-256 values checked against the generated manifest. Both
  new FAPs are in Base and absent from updater resources. Duplicate Toyota V1
  is absent from resources/ZIP; a stale exploratory build file was not staged.
- SDK/ZIP integrity and updater TGZ reading passed. No production audit pins,
  accepted-source ledgers or Community ABI findings were changed.

### Measured production logic coverage

The native fixtures execute actual C bodies; device I/O/GUI adapters are not
counted. This is coverage of these four engines, not the complete firmware,
inherited RF decoders, RTOS or hardware capture loop. All39 functions ran.

| Module | Line coverage | Region coverage |
| --- | --- | --- |
| field_io | 96.67% | 88.60% |
| field_round | 98.04% | 88.54% |
| field_bundle | 93.17% | 91.92% |
| field_inventory | 94.61% | 90.40% |

Generation/size/magic are covered by checked-record digests. Import is bound to
the inspected receipt; changed valid bundles or inventory baselines fail rather
than producing a mixed result. Reports use content-identified create-new names.
Failed imports may leave only new unpublished copies inside their own imports
directory, never overwrite an existing capture or publish a half-verified card.

### Candidate artifact receipts (not published)

These identify the exact validated build, not downloadable release assets.
Large local outputs are removed after checks under the owner's cleanup rule.
Recreate from the source commit and installed toolchain, then revalidate the
new build before publication. A future merge/tag must be rebuilt at its own SHA.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| full DFU | 876837 | `ac51826575f4920722fb7fd675e903f83b8c44a37c9223b98f30a14686174940` |
| SDK ZIP | 24600665 | `30188ed795809b41049ec3f64542f2c64bff4565fa309f37fe423816d6c6369e` |
| updater TGZ | 4670307 | `7f3e68d11f59f76f48088b8dcac091c3f92c6e3a36d61299cd40dcd58824d52f` |
| package manifest | 48636 | `1aebc5fe73147d9868593b4b4203799731e7383d5426ce0343117e0e6578890a` |
| packages ZIP | 5828398 | `6be8f959203d96e9681032b2e6092182496d243ff4ff95fa07bdc389bd38ccb8` |

## Delivery and remaining acceptance

- Firmware, matching independent FW Packages and the narrow Companion policy
  patch must be published separately after authorization. Existing published
  Dev031/FW Packages027/app1.11.31 remain untouched. No tag, push, PR, external
  CI dispatch, device connection, flashing, capture or SD mutation occurred.
- Object/round/inventory exports may contain private data. Use an SD reader or
  USB SD mode to copy them off Flipper. Companion Files currently uploads copies
  back but has no generic download action; the UI no longer claims otherwise.
  Inventory cannot restore missing file bytes or identify their deleting app.
- Independent agent review **not performed** under this turn's no-delegation
  instruction. Local tests/manual source review are not independent review.
- Hardware checklist (owner's devices/captures only):
  - [ ] Install matching firmware then FW Packages; verify the42 protocol leaves.
  - [ ] Open old cards, launch linked RFID/iButton/NFC menus without auto-emulation.
  - [ ] Round resume/reboot, notes/results, repeated report export and SD failure.
  - [ ] Export/import a disposable object under a new name; compare original
    files/hashes. Corrupt/changed packages must not publish a card.
  - [ ] Snapshot disposable captures, edit/add/remove copies, compare all result
    categories; cancel scan and verify no source deletion/baseline replacement.
  - [ ] Nice own52/72-bit recordings and new passive RX protocols: receive, save,
    reopen and compare full payloads. No new automotive transmit flow exists.
  - [ ] Band Map INT/EXT/dual, peaks, busy broker, external unplug, exit cleanup;
    verify sweep speed/calibration on hardware before claiming improvement.
  - [ ] Passive NFC: own active reader/card, capture/cancel/no-field/busy cases,
    complete card replies/overruns, manual Save, native NFC working after exit,
    concurrent BLE and normal idle operation. Package remains experimental.
- Existing idle-OOM and iOS connection reports are not declared resolved by this
  field-feature work. No new feature runs as a resident/background service.

## Output cleanup

- After all build/archive checks, `lsof` found no open files under the four
  task-output directories; no Git directories/object stores were inside them.
  The shared Git store and installed toolchain were not removed.
- Standard `fbt -c` removed tracked build-system outputs first; the remaining
  task-created directories were removed without forced deletion:
  `build/f7-firmware-C`, `build/f7-updater-C`, `dist/f7-C`, and
  `/tmp/tumo-field-kit-20261026.QNpS58`. The dangling `build/latest` symlink and
  this candidate's generated splash stamp were removed as well.
- Directory allocation before cleanup:412528 KiB (about403 MiB). Immediate
  `df` available-space change:460359048 ->460779820 KiB, **+420772 KiB**
  (about411 MiB). APFS/background activity can make that differ from `du`.
- Sources and unpublished commits remain in the reused worktrees. Recover
  binaries by rebuilding the recorded source commit; no large backup archive
  was created. SD files, signal recordings, crash evidence, shared caches,
  simulator runtimes and other checkouts were not cleaned.
- Only compact preview sheets, the generated package manifest and coverage
  summary remain in `docs/assets/field-kit-2026-10/` and
  `docs/product/field-kit-2026-10-evidence/`.
