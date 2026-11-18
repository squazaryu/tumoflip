# Gallagher EM4305 write-frame correction

Selectively adapted from [Unleashed 5ab2996a / PR1217](https://github.com/DarkFlippers/unleashed-firmware/pull/1217),
authored by Firat Hajiyev. The upstream fix and tests are AI-assisted as disclosed
in that PR. The source remains under the repository's existing GPL-3.0 license.

## Scope

Gallagher writes previously consumed encoded bits1 through96 instead of0
through95. That shifted all three EM4305 data words by one bit and allowed the
unused next-preamble byte to affect the result. The existing shared builder now
fills words5–7 from exactly the same 96-bit frame as T5577, reversing each word
for EM4305 transmission. Configuration word4, RF/32 Manchester, mask0xF0 and the
request's other fields are unchanged. Gallagher does not request inversion.

This changes only the image prepared by the existing explicit EM4305 write
action. Reading, emulation, T5577, other protocols, chip-selection settings,
password policy, public API88.23 and private ABI1005 are not changed. No FAP is
added or moved. Existing saved RFID files remain valid; no cleanup, re-save or
data migration is needed.

## Verification

- RED: compiled actual callbacks reproduced the shifted data words. GREEN:
  independent byte/bit oracles accept every one of96 single-bit inputs, constant
  and alternating frames, both tail-byte polarities, request guards and refusal
  of an unsupported target.
- Actual Gallagher encoder/write/CRC/decode code is compiled on host for259
  fixed and deterministic synthetic credentials. EM4305 words match the T5577
  frame and reconstruct the same normalized credential; CRC corruption is
  rejected. No RF transport or physical card is involved in this test.
- Differential testing retains the frozen pre-refactor fixture for20 protocols;
  only the three intentionally corrected Gallagher EM4305 data words use a new
  independent oracle. Other request/protocol bytes and unsupported targets must
  still match the old implementation. The historical fixture is not rewritten.
- Tests execute with fatal ASan/UBSan. `measure_gallagher_em4305` reports coverage
  only for the named production writer and shared builders, excluding fixtures;
  it is not whole-firmware or hardware coverage.
- The device unit-test suite registers a ProtocolDict-based Gallagher regression
  with three credentials. Building that image is not the same as running it on
  hardware; on-device execution is still pending.

Commands (use the installed repository toolchain):

```sh
PYTHONPATH=.:tools/tumoflip toolchain/current/bin/python3 -m unittest tools.tumoflip.test_gallagher_em4305 tools.tumoflip.test_lfrfid_write_fill
PYTHONPATH=.:tools/tumoflip toolchain/current/bin/python3 -m tools.tumoflip.measure_gallagher_em4305
```

Before hardware acceptance, run `unit_tests lfrfid` on a test image, then use an
owned/authorized Gallagher sample and writable EM4305 tag. Explicitly select the
EM4305 target, write, read the tag back, and compare region, issue, facility and
card fields with the source. Do not infer a successful physical write from host
tests or the prepared request alone. This adaptation does not flash a device or
perform any card write automatically.

At a later Dev publication, retain the exact firmware/FW Packages snapshot
contract even though the API/ABI did not change. No package bytes, source pins,
audit decisions or app version are advanced by this source-only correction.
