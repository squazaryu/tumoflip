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

## Implemented candidate / installation routes

All new application code remains external and package-only; public API stays
88.23. A firmware install alone does not install these FAPs. The next independent
FW Packages Base content update must include the five new binaries and the two
updated hosts below; do not activate a catalog or reuse a published tag merely
because local tests pass.

| Application | Route after the Base package is installed | Scope |
| --- | --- | --- |
| Servo Tester+ 1.0 | Apps / GPIO / Servo Tester+ | PA7/pin2, 50Hz hardware PWM, explicit Start, session calibration, external power by default |
| Power Journal 1.0 | Apps / Tools / Diagnostics / Power Journal | 5s app-only telemetry, 128 points, four checked SD slots, no charge control |
| Sensor Watch 1.0 | Apps / Sub-GHz / Sensor Watch | Internal CC1101, AM650, manual315/433.92/868.35MHz selection, 25 existing RX decoders |
| Varta Inspector 1.0 | Apps / RFID / Varta Inspector | Cancellable15s ASK EM4100 observations and strict saved .rfid/.t5577 inspection |
| Tumo UHF Inventory 1.0 | Apps / GPIO / Tumo UHF Inventory | Separately powered UCM601 UART reader, explicit ≤60s inventory, 32 EPCs, no tag mutations |
| Device Library 1.3 | Apps / Tools / Device Library / object / Sensor Watch | Validated object namespace; per-object sensor history, no file deletion |
| TumoSpectrum 3.6 | Apps / Module One / Signals / TumoSpectrum / Sensor Watch | Deferred handoff after releasing RF resources, then return to TumoSpectrum |

Sensor Watch does **not** require Module One despite the existing TumoSpectrum
menu location. UHF does require an additional UCM601-compatible reader; Module
One and ESP32-C5 are not substitutes. No new ESP32 backend or flash operation is
part of this batch.

History/CSV data stays under `/ext/apps_data/device_library/`. Exports are
create-new files in `reports`, synchronized, closed and re-read for SHA256/size
comparison. An error removes only the new incomplete export, never an existing
capture. Power and sensor histories retain four checked record slots. A corrupt
history opens read-only, not as an empty writable replacement. Sensor histories
save on Stop/Back/normal exit, not after a crash or an unexpected power loss.

No iOS schema/UI change is required for these routes: they use the existing Base
catalog and loader. New IDs/paths are distinct from ordinary Community `varta`,
`uhf_expansion` and `gbhobbytech_servo_tester`; those apps remain available.
The existing Device Library data-family protection already covers new histories
and reports. Do not claim a new TumoCompanion build was made for this batch.

## Source / behavior boundary

- Servo is a new model/backend/UI inspired by upstream0.5, retaining the MIT
  notice and original icon. The notice is embedded in its FAP assets as well as
  included in source. Output polarity/timing is not hardware-certified by mocks.
- Power math and persistence are new code. Battery Guardian is an MIT-licensed
  idea/reference, not a copied binary. An observed capacity appears only after
  three qualified ≥15% discharge segments; USB/charge/gaps reset unfinished
  inference. Gauge health and observed mAh are different measurements.
- UHF's pinned MIT source documents the UCM601 frame contract. The new parser
  rejects bad checksums, wrong addresses/lengths and mismatched PC/EPC sizes;
  there is no heuristic scan of arbitrary payload offsets. Signal/frequency
  metadata remains labeled raw rather than inventing calibrated dBm/Hz units.
- Varta's GPL3 source supplies format/prefix/marker evidence. The new inspector
  verifies EM4100 row/column parity and honors block-read masks, but does not
  reconstruct keys, issue password reads, capture reader passwords or write tags.
  Password fields/block7 are not imported into its reports. The upstream warning
  about protected reads acting as irreversible writes on blank tags is why no
  mixed read/write flow is ported. A generated dump can match the same constants;
  neither prefix nor marker proves authenticity or access authorization.
- Sensor Watch compiles the existing local GPL-compatible Weather Editor
  decoders into its own FAP; the shared registry header no longer pulls the
  Weather Editor UI into decoder consumers. No SubSpotter source is imported.

## Emulator — concrete hold, not a successful boot test

`python3 -m tools.tumoflip.emulator_preflight <pinned-checkout>` is read-only.
On this Mac the pinned source matches, but its own Flipper peripheral models
have no explicit root license, the supplied runtime builds target Linux, and
Renode is not installed. The third-party Renode MIT license is not evidence of
terms for the new models. No upstream installer, firmware patches, runtime,
32GB SD image or machine-wide dependency was executed/created. No emulator code
is redistributed in the Dev/FW Packages payload. Resolve model terms and a
supported runtime before adding an actual bounded boot harness; the proposed
experimental image budget is64MiB, not a claimed working SD configuration.

## Manual acceptance after publication

1. Servo: measure PA7 PWM with no actuator first; check polarity/50Hz and
   500..2500us limits. Then test only a safe unloaded fixture. Back/Stop must stop
   the signal; GPIO5V must be restored only when this app enabled it.
2. Power: compare shown charge/voltage/current/temperature with stock diagnostics;
   close/reopen and verify no discharge inference bridges the closed interval.
   Test export/read-only behavior on a spare SD. No week-long idle sampling claim.
3. Sensors: receive a known owned sensor; compare decoded ID/channel/units with
   Weather Editor. Check Prev/Next, age/clock change, graph range and object/global
   isolation, Stop/reopen persistence and CSV. Decoder fixtures are not RF proof.
4. Varta: read own EM4100 at supported rates, cancel and verify stock RFID still
   starts. Compare owned saved dumps; missing block flags must not become positive
   marker evidence. Input files must remain byte-identical.
5. UHF: use the correct powered reader, UART115200 and3.3V logic, not a generic
   ESP32. Verify version, inventory/duplicate counts, actual stop on Back/timeout,
   UART release, CSV and reader unplug behavior. Stop cannot be verified over a
   disconnected UART; reader region/power settings are intentionally unchanged.
