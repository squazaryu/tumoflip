# Quac 0.9.2 lifecycle hardening

This is a narrow Tumoflip implementation of the safety corrections identified
while reviewing [upstream a46cffc](https://github.com/rdefeo/quac/commit/a46cffc15fcd437587e9b013ccc21c9439c7ab5e)
under [#442](https://github.com/squazaryu/tumoflip/issues/442). It does not import
the Picopass/Loclass feature range or establish parity with the full upstream
release. The existing `popup_free` correction was already present and is unchanged.

## Fixed boundaries

- `actions/action_subghz.c`: initial file parsing/copying and decoder loading
  must succeed before TX is requested. A rejected TX start must not install a
  RAW callback, wait for completion or apply the normal transmission delay.
  Failures populate the existing error string, which causes playlists to abort.
- `actions/helpers/subghz_txrx.c`: a false return from the internal/external
  device's asynchronous start is an error, not success. Failed-start resources
  are released and the radio returns to idle without the dynamic save callback.
- `actions/action_ir_utils.c`: a reusable signal owns at most one RAW allocation.
  Release previous ownership before loading another command; publish the new
  allocation only after a complete read. All failures remain safely destructible
  and every temporary string is freed. Empty timing arrays are rejected before
  allocation/transmission.

Valid parsed/RAW IR reading and writing, individual/bulk import, default Sub-GHz
frequency, custom presets, decoded duration and successful RAW completion remain
supported. Radio-broker ownership, launch routes, firmware API and user settings
formats are unchanged. No user signal files are modified by the test harness.

## Verification

From the exact checkout, using the firmware-toolchain Python:

```sh
python3 tools/tumoflip/test_quac_lifecycle.py -v
python3 tools/tumoflip/test_quac_lifecycle.py --coverage
CC='clang -fsanitize=address,undefined -fno-omit-frame-pointer' \
  python3 tools/tumoflip/test_quac_lifecycle.py
FBT_NO_SYNC=1 ./fbt -j2 COMPACT=1 DEBUG=0 fap_quac
```

The host harness extracts the actual production C functions and signal type,
not a reimplementation of their logic. It injects storage, parser, allocator
and device-start failures without transmitting. Its 59 behavior cases cover
the original failures and valid controls, including RAW-to-RAW, RAW-to-parsed,
parsed-to-failed-RAW reuse and IR writer failures. Coverage enforces at least
80% line and region coverage for each extracted production boundary. It does
not execute full FreeRTOS/UI/playlist or radio hardware lifecycles.

The suite runs in PR and release CI. Quac is an explicit PR build/APPCHK target.

## Separate limitations and hardware acceptance

- The shared RAW worker can fail its later asynchronous file reopen after the
  initial action load succeeded. Its current API reports thread startup before
  that open completes. This can still leave a caller waiting indefinitely;
  shared-worker error propagation requires a separate scoped change. These
  tests and fixes do not claim to close that race or all SD-disconnect failures.
- Existing dynamic Sub-GHz save-back storage semantics are unchanged; this is
  not a transactional rewrite of saved signals.
- Before release acceptance, use owned signals to test direct actions, links,
  playlists, raw/parsed IR Import All, decoded/RAW Sub-GHz on internal and
  external radios, denied TX, cleanup/Back and repeated app launch.
- Hardware acceptance and publication remain separate. This patch does not
  publish firmware/FW Packages, install files or close the broader Community
  Pack author/provenance gate.
