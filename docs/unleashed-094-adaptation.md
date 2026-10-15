# Unleashed 094 selective adaptation

Baseline: Tumoflip Dev 009-024, `a91c1323d12a87bdd391dcc49428cd1b9e595d1a`.
Upstream: immutable `unlshd-094`, `2f62b3ab5d767c41e09ab17c6b5fbd175c8c4c85`.
This document records implementation scope, not hardware acceptance or publication.

## Radio safety

Source `e1ebaa318b6dec205c2dce4575844b8e9bd56c3f`: preset misses return NULL
without unsigned array indexing. Standard, ARF, SubGHz Remote, Companion,
Quac, Wardriving and Weather entry points stop instead of reusing an old preset.
ARF Security+ generation uses a local transmitter and releases it on every path.
The SubGHz Remote dependency is pinned to published guard commit
`01be7a0d048b1346c02661e215283c67349e7f7c` of `squazaryu/SubGHz_Remote`.

## Receive handling

Sources `ea01dfccfd324db081c3ab793c33c0fd71492a68` and
`f67bb1fff48e68bfd14a5f8556a10f03dc220523`:

- Nice O-Code 112-bit reception retains the 52-bit body and 60-bit tail (`Data2`).
- Plain Nice press grouping preserves button bits, not just the low body word.
- Standard/ARF histories use exact, bounded press identity for affected protocols;
  hash collisions, another button, counter, transmitter or frequency do not merge.
- Came Atomo inspection no longer transforms the original saved key in place;
  signed-shift undefined behavior in field reconstruction is removed.
- Unknown installer codes use `ICKnown: false`, not a reserved numeric IC value.
  Explicit legacy `IC: FF FF` remains valid. Unknown-code and unsupported long
  frames fail before encoder upload/counter updates, including through RPC.
- Raw parsed captures remain saveable for analysis. Do not use receive-only
  captures with older firmware that does not understand the new marker; use
  pulse RAW captures for cross-version analysis.

F7 API **88.20** adds three query helpers. Existing exports and public structure
layouts are retained. No MJS, GPS or Network exports are imported.

## FAP radio ownership

Nice O-Code and Security+ PIN use a FAP-local shared lease helper, not upstream's
direct OTG toggling. It preserves pre-existing power, handles busy/begin/frequency
failures, releases the device/registry before the lease, and handles disconnection.
External Security+ transmission is available for explicit user sends only;
the existing automatic sweep retains its original internal-radio scope.
No Seed Capturer, new recovery algorithm or new sweep mode is introduced.

## HomeGate

Source `e4eda9efca0177401c4b34872b6e366668a80041`: HomeGate uses 433.92 MHz,
AM650 and the existing KeeLoq generator. Its menu ID is appended as **102**;
all prior IDs and the private feature-plugin ABI remain unchanged.

Only `keeloq_mfcodes_extended` is refreshed from the immutable upstream store:
SHA-256 `a2f5bb2294671c7e73cefe4a20d011e86d6a9aaa609582f0431ecae9a76cea68`.
The primary and user keystores are not replaced. Encrypted data is not decrypted
or printed by host tests; receiver/key-store acceptance remains hardware work.

## Ranged storage reads

Source `51045761d3ff6862f3999141c5c2eb4288c6b319`, adapted without adopting the
upstream protobuf fork's unrelated GPS/Network messages:

- A repository-owned `storage.proto` overlay appends `ReadRequest.offset = 2`,
  `ReadRequest.size = 3`, and `ReadResponse.ranged = 2`.
- Zero size means through EOF; offset beyond EOF produces an empty acknowledged
  response. The range is computed by subtraction, never overflowing offset+size.
- Legacy requests still read the whole file. Errors remain errors; partial read
  buffers are released, not sent as successful chunks or leaked.
- Clients must require `ranged` on every response before treating data as a slice.
  This is not a background-BLE guarantee or an automatic installation-resume API.

## Acceptance remaining

The initial real ARM build adds 1680 bytes relative to Dev 009-024 and has
2448 bytes of section headroom, with no C1/C2 address overlap but no completely
unused 4096-byte page. A scoped `-Oz` experiment on the Sub-GHz library did not
recover meaningful space and was not retained; HAL/loader flags stay unchanged.
The owner requires restoration of the full 4096-byte reserve before publication.
The release gate is unchanged. Compact private button lookup tables and one-byte
label IDs replace repeated switches/pointers, without removing protocols, exports
or button actions. Golden results from `523c93741b8259a3abb642e8aed1738f6915900a`
cover all 256 original codes × 256 directions for each of 11 protocols, plus
all 35 protocol label sets and fallback behavior. The first compact ARM build
ends at `0x080D5E68` in both ELF and flashed DfuSe ranges: 4504 section bytes,
4096 physical bytes after erase-page rounding. Recheck the final release ref;
these measurements alone do not authorize publication.

Host fixtures cover invalid presets, generator ownership, synthetic Nice frames,
legacy files/RX-only transmission, collisions, broker cleanup and RPC failures.
CI runs them; actual F7/FAP builds, release validation and physical acceptance
must be recorded separately. Check owned receivers/cards, external unplug/replug,
ordinary Saved/RAW paths and older/newer Companion compatibility after installation.
