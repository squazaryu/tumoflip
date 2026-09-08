# Wardriving: idle RX memory reclamation

Selective adaptation of all-the-plugins `94870100905aac5151eb828140a74fff248e566a`,
tracked in #461. This is not a full import of that commit or `198c6848ba`.

## Scope and lifetime

- Menu startup does not allocate the worker or protocol decoders. The startup
  database warning still uses the actual keystore load result, not file presence.
- Returning to Start or entering Saved's file browser stops RX/TX and releases
  worker, receiver and environment. Release invalidates borrowed decoder pointers;
  signal detail/configuration scenes deliberately retain the pipeline.
- Read, decoder lookup, receiver access and transmit setup recreate it on demand,
  restoring the saved filter, callback and callback context. Radio broker ownership
  and user settings are unchanged.
- RX shutdown disables the hardware producer before stopping/joining the worker.
  Active RX/TX, a live transmitter or a running worker prevents release. Repeated
  idle release is safe; transmitter and decoder pointers are cleared after free.
- GPS arithmetic, the shared worker, its 4096-sample capacity and 2048-byte stack
  are unchanged. No private worker fork, API change or buffer reduction is included.

Memory is reclaimed while idle, not during active capture. Its exact amount and
fragmentation on a real device have not been measured. Allocation uses existing
firmware allocators and their fail-fast out-of-memory policy; this patch does not
promise recoverable allocation failure or globally solve low-memory crashes.

## Validation

```sh
python3 tools/tumoflip/test_subghz_wardriving.py
python3 tools/tumoflip/test_wardriving_lazy_rx.py
./fbt fap_subghz_wardriving -j2
```

The host fixture compiles actual production lifecycle functions with mocked
radio/decoder services and AddressSanitizer/UndefinedBehaviorSanitizer. It covers
100 release/recreate cycles, callback/filter preservation, repeated release,
active guards, environment-only database checks, missing database and producer
shutdown before worker join. Optional macOS LLVM coverage:
`WARDRIVING_HOST_COVERAGE=1 python3 tools/tumoflip/test_wardriving_lazy_rx.py`.
Source guards preserve the original GPS and worker bytes, and check scene wiring.
These tests do not simulate RF traffic, the SD card or full Furi scheduling.

PR and release CI run both focused test files and explicitly build the FAP.
Broad local regression run on Python 3.11: 847 tests, 844 passed, three failures
also reproduced on the unchanged baseline tree (Mosgortrans unknown-layout text,
TumoSpectrum hopping symbol assertion, MF Ultralight stale API 88.4 assertion).
These unrelated pre-existing assertions were not changed to make this PR green.

## Hardware acceptance (pending)

1. Open Wardriving and record free heap in Start. Open Read, then return to Start
   ten times. Check reclaimed memory, no progressive decline and responsive Back.
2. Receive an owned test signal; open details/configuration and return to Read.
   Confirm frequency/modulation, filters, decoder results and hopping still work.
3. Start -> Saved -> cancel, repeatedly. Then open a known valid saved `.sub`;
   check details and return. Try a malformed file; the error must not hang the app.
4. If testing transmit with an owned receiver, stop it and return to Start;
   verify no stuck transmission and that a new Read session works.
5. Repeat the capture/exit path with internal and external CC1101 where available;
   verify another Sub-GHz app can acquire the radio after Wardriving closes.
6. Check UART GPS fix display and saved coordinates with GPS on and off. GPS
   computation is unchanged; no new approximation is present.

No hardware acceptance or package promotion is implied by compilation. Publish
the rebuilt app through the normal FW Packages flow after the release decision;
do not accept arbitrary Community Pack bytes as this customized build.
