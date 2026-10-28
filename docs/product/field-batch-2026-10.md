# Field batch — next Dev candidate (2026-10-28)

Authorized: the complete set selected in the preceding upstream/Reddit review.
This is an implementation candidate, not publication or hardware acceptance.
Show actual native UI previews before a new release; do not flash a device.

## Scope / ready criteria

1. **Varta:** adapt owned-key/dump diagnostics to the local LF/T5577 contracts;
   validate inputs and cancellation/coil ownership. No new reader-password
   harvesting or automatic access-key reconstruction workflow. Preserve existing
   firmware capabilities; upstream mixed flows are not a blind cherry-pick.
2. **Servo Tester+:** hardware PWM rather than CPU bit-banging; explicit arming,
   bounded pulse calibration, stop on Back/error, restore only app-owned 5V.
   Display pulse width, not an unmeasured claim about physical shaft degrees.
3. **Live sensor cards:** reuse licensed local Weather Editor RX decoders, not
   unlicensed SubSpotter code. Known protocol/ID/channel, latest values/age,
   bounded history and checked CSV export; integrate TumoSpectrum/Device Library.
   Do not duplicate existing Band Map or claim a guessed family is an identity.
4. **Power history:** passive telemetry/history and qualified estimates, no
   charge-limit/control commands. Explicit app-only sampling unless a separately
   measured existing-service hook can satisfy the resident budget. Do not imply
   an external FAP samples a week of idle time after exit.
5. **UHF inventory:** exact UART protocol/board evidence, bounded parsing and
   checked export, no automatic RF start or tag-lock/kill/write operations.
   Additional UHF hardware is required; Module One/C5 alone is not the reader.
6. **Emulator:** a separately pinned experimental development/test adapter, not
   a firmware app or RF/BLE acceptance. Inspect setup before running it; do not
   generate the upstream 32GB SD image or install system-wide tooling implicitly.

Package-owned code should stay lazy/external where possible. Compare actual
resident ELF/DfuSe sizes, retain the full4096-byte physical reserve and keep
firmware, FW Packages and iOS delivery states separate. Native/synthetic tests
never establish physical-device acceptance.

## Immutable upstream inputs

| Project | Exact source | License / role |
| --- | --- | --- |
| mishamyte/varta | 1c333482c0e425107374a17e8a06472871efe67b | GPL3; mixed LF app reviewed selectively |
| gbhobbytech/flipper-servo-tester | 50b01db47d4fefcfa2fb9a41075847c43c2b1fc7 | MIT / M.Hasbini; concepts and wiring UX |
| 0xKernelEclipse/battery_guardian | b26b4b058fbe4caf40250fed9b095b08e3ce91a6 | MIT; passive telemetry/estimate concepts |
| mtoolstec/fz-uhf-expansion | 5a5f9e8d2689f4382bb4e8c3b46eb67ba4413011 | MIT; UCM601 UART wire contract |
| d4rks1d33/Flipper-Zero-Emulator | 6b2443fc0065f102ea3045024b1d5e65f4d4c68c | verify runtime/models/terms before execution |

SubSpotter is an idea source only: its repository has no explicit license.
Existing local Weather Editor is the licensed decoder source.
