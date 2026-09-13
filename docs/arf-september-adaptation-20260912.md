# ARF September adaptation — work in progress

Base: Tumoflip dev `30258ecfd370bf0c031751838d7ac92e0b4b3ea1`.
Branch: `feat/arf-september-adaptation`.
ARF snapshot: `9fabe2de18971eaf35e3bf667cde17e3d0ec560a`.
No firmware or package release is authorized by this document.

## Weather Editor

Imported `applications/system/Weather-Editor-EN` as a package-only candidate.
The directory was introduced by `6fed312cf32acc8e298e8a06156b4a4cce1f76ee`.
See its `UPSTREAM.md` for provenance, safeguards and remaining gates.
This is not yet a managed FW Package or a replacement for Community Apps.
API remains 88.7.

## ab7bbf2a

Adapted only the Kia V5 file-length compatibility slice in both core and
ProtoPirate parsers. Accept exactly 64 or 67 bits without rewriting the
recorded payload or length; propagate file-read errors.

The mass button changes are not integrated. CAME and Chamberlain explicitly
enable four directions while all directions repeat the same original code.
Ansonic uses zero as both a valid button and an unset marker; its advertised
alternates are not necessarily distinct. Dooya maps Right to P2, which must
not silently become an ordinary directional action. A useful integration
needs per-protocol action semantics and tests, not a global D-pad enable.
No upstream infinite-repeat or automatic 64-to-67 normalization was copied.

## 9fabe2de

Reviewed, not integrated. Renault V1 is coupled to removing non-0x13 variants
from Renault V0. Copying that removal without a fully compatible replacement
would regress existing captures. Current Renault V0 routing is retained.
The Kia V0 changes concern Honda transmit-frame reconstruction, not merely
display labels. The BMW change is a comment/reference, not functionality.

Secret-key recovery and vehicle-security bypass extensions are excluded from
this work. The receive/file compatibility and diagnostic portions require a
separate bounded comparison with the existing Standard/ProtoPirate path;
they are not represented as completed by the Kia V5 fix.

## Software evidence

Run from this worktree using the pinned toolchain:

```sh
./toolchain/arm64-darwin/bin/python3 -m unittest \
  tools.tumoflip.test_kia_v5_file_lengths \
  tools.tumoflip.test_weather_save \
  tools.tumoflip.test_weather_validation \
  tools.tumoflip.test_weather_load_transaction \
  tools.tumoflip.test_arf_replay_fixes \
  tools.tumoflip.test_subghz_protocol_packs
./fbt -j2 FBT_NO_SYNC=1 fap_weather_editor
```

The first command covers 14 tests. Save tests execute production C paths
with fault-injected storage boundaries; they do not simulate the complete SD
filesystem. Coverage percentage has not been measured. Compilation and host
tests do not establish RF, UI, install, or physical-device acceptance.

`./fbt -j2 FBT_NO_SYNC=1 fw_dist` completed successfully on this branch.
This is a local validation build, not a new version or published release.
