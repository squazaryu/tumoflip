# Work Profiles and Decoder References

Implementation candidate based on `t-dev-009-008`. Not a published release;
visual review and physical acceptance are still required.

## Work Profiles

Open **Sub-GHz > Work Profiles**. Save current as creates a new named
`/ext/subghz/profiles/<name>.subprofile`. Existing profiles are never overwritten.
Names use 1–24 ASCII letters/digits/spaces/hyphens/underscores, without leading
or trailing spaces. Load profile opens the file browser, then a scrollable review
screen with an explicit Apply button.

Schema 1 stores separate Standard and Read RAW frequencies and preset names,
plus the shared Protocol Pack, hopping mode and radio selection (internal,
external, dual). Presets resolve by name, not an unstable picker index. The
radio validates both frequencies before applying. Missing presets/radio or an
incomplete Protocol Pack abort application; failed runtime switches attempt to
restore the old pack/radio and report a restore failure explicitly.

Loading is session-scoped: it does not automatically replace startup defaults or
transmit/start recording. Profiles themselves survive reboot on SD. Subsequent
manual configuration changes continue using the existing settings-save behavior.
The application does not promise that disconnected external hardware can be
restored. The format does not embed custom preset register blobs: the same named
preset must be available in the current settings. Filters, TX power and amplifier
settings are deliberately not modified by applying a work profile.

UI/storage logic is in `subghz_workspaces.fal`, loaded only while the screen is
open. Its callbacks never change host scenes: top-level Back returns to the host
before the module is unmapped. Shared GUI callbacks/borrowed input buffers are
cleared before unload, including app teardown. Private Sub-GHz feature ABI is
1002; all three feature FALs must be rebuilt/delivered together. Public F7 API
remains 88.13. Updater validation now requires the new FAL and checks its hash;
host import validation covers all three modules.

## Decoder References

Open **Tumo Acceptance > Decoder References**:

1. Add RAW reference, select an existing `.sub` RAW file and its Protocol Pack.
2. Inspect decoded frame count and protocol labels.
3. Back > Save as reference explicitly records the current expected result.
4. After updating, Check all references reruns those captures offline.
5. Export last report saves a new private text report without overwriting one.

The baseline reflects a user-confirmed observation, not independently established
protocol correctness. No reference can be saved after cancellation, malformed
input, zero decoded frames or another failure. Existing references are never
silently regenerated. Remove an unwanted `.tref` using the normal file browser;
there is no automatic cleanup of user captures.

References live at `apps_data/tumo_acceptance_suite/references/reference_NN.tref`.
They store the original SD path, pack, frame count, firmware/API provenance and
SHA-256 of both source bytes and length-delimited serialized decoder frames.
Keep original RAW files. Changed source bytes produce **Input changed**, rather
than a decoder regression. Different serialized results produce **Decoder result
changed**. Missing/corrupt files, incomplete packs, read/serialization errors,
timeouts and interrupted runs cannot count as matches. Decoder formatting and
keystore changes can also change the result; a mismatch requires investigation,
not an automatic assertion that the new firmware is wrong.

The runner feeds timings directly to the existing protocol receivers. It never
initializes a radio device, acquires RF hardware, transmits or writes a capture.
Serialization uses a fixed neutral preset header (AM650); modulation is not
applied to hardware and this is not a modulation/RF sensitivity test. The system
keystore must load; optional extended/user keystores follow normal Sub-GHz setup.

Limits: 32 reference slots, 4 MiB/500,000 pulses/120 seconds per capture,
10,000 decoded frames, 8 KiB per serialized frame. RAW parsing is incremental
(bounded metadata, integer overflow/zero-duration checks) and runs in chunks on
the dispatcher. Back cancels between chunks. Display models and report writes
are not driven from the GUI input callback. Corrupted or oversized metadata is
rejected rather than guessed. A single pack is tested per reference; add separate
references when comparison across multiple packs is required.

## Verification and delivery

- Native sanitizer tests exercise profile validation/application/rollback,
  RAW streaming boundaries, result mismatch/read failure/timeout/cancel paths,
  module lifetime contracts and offline-only boundaries.
- `measure_workspaces_coverage.py` measures only the native validator/parser;
  it does not claim GUI, storage or hardware coverage.
- `render_workspaces_corpus.py` renders production profile/progress code with
  repository GUI draw functions/U8g2 fonts and controlled display fixtures.
  Long values, scrolling, errors and cancellation are included.
- Build firmware, the three feature plugins and Tumo Acceptance 0.3.0. The
  Acceptance FAP is shipped in firmware resources and the paired snapshot ZIP.
  Independent Dev 020 does not manage this target, so it cannot replace the new UI;
  do not migrate its ownership merely to publish this firmware update.

Hardware acceptance remains open: save two profiles with distinct Standard/RAW
settings; alternate them and test Back at every page; missing preset/SD/external
radio; repeated entry/exit without heap drift; add a known RAW capture, save,
verify a match, then test changed/missing/truncated input and cancellation; confirm
the real LCD layout and responsiveness, and export/read the result report.

### Local evidence (2026-09-22)

- 996 host tests passed with the bundled toolchain Python 3.11.
- Native validator/parser: 100% line/function coverage, 90.9% branch coverage
  each. Other layers are not included in those percentages.
- F7 firmware, all three feature FALs and Acceptance FAP built. The feature
  import checker resolved every import against the actual firmware/private table.
- C1 ELF/DfuSe end `0x080D2C38`: +456 resident bytes versus 009-008,
  17,352-byte section gap and unchanged 16,384-byte physical C2 reserve.
- 26 native-rendered UI fixtures reviewed, including scroll/error/cancel states.
  These are host previews, not physical LCD or full interaction acceptance.
- No remote push, release, user-device flash or automation changes were performed.
