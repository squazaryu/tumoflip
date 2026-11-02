# GUI redraw, defensive IR and core test profile

Source-integration work after Dev009-035. This is not a release, device flash,
Stable promotion or a guarantee that historical idle OOM/BLE failures are fixed.
Published009-035 and independent catalogs remain immutable. Public API88.23 is
unchanged; the normal updater must keep at least4096B physical C2 reserve.

## Reviewed sources and local adaptations

- Unleashed PR1198 /01307e91008025b453800589d480c7e232a0dd59: a private
  ViewPort flag coalesces covered-port redraw requests. Exposing a port still
  forces a GUI redraw. Label scrolling runs only between view enter/exit.
  BT status changes explicitly redraw while preserving our beacon/transfer
  widths; SD mounted/error icon changes redraw without fake mount events.
- Local timeout adaptation: failed mutex acquisition never suppresses an
  update, unlocks another owner or mutates the visibility flag. The old
  non-blocking draw fallback is retained and queues a locked redraw to re-arm.
  Timer start/stop/free commands are outside model locks, including ButtonMenu
  teardown, to avoid a timer-daemon/model-lock inversion.
  ButtonMenu owns its timer in the controller, not its drawing model. Repeated
  blocking and bounded ViewPort lock bodies are shared; mandatory checks,
  timeout, ownership and diagnostic lines remain. GCC cloning is disabled for
  the small shared timeout helper, not for the whole firmware. This removes
  duplication rather than functionality and preserves the required C2 page.
- Local IR fix: unknown database button names, empty databases and invalid
  parsed signals return existing errors rather than relying on release-disabled
  assertions. Scratch allocations close on every path. Registered names, offsets
  and valid EOF remain. No file modification or transmission is added.
- Selected PR1199 /f76ea424849281bc47907bdbbc0804d278f39390: remove the core
  test resolver's dependency on the excluded JS host; fix projector filename
  and six button registrations. Add two read-only IR error fixtures. Unlike
  upstream's deletion, preserve all8 historical JS source files byte-identically
  under `tools/tumoflip/fixtures/deferred_js_tests`, with source hashes and an
  explicit non-executed status. No JS runtime or new SDK exports are introduced.
- Artifact review also exposed8 missing imports in test_nfc/test_subghz. Link
  existing private UL-AES/Kia code and AES dependencies into their own test FALs,
  not the resident production image. The separate diagnostic validator checks
  actual DFU ranges/CRC,30 declared FALs, real host imports and archived bytes.

PR1195's retained Apps browser/cache is **not** copied; `loader_applications`
still releases the directory browser before launching a FAP. Deferred Desktop
retained-frame#228 is not resumed. No signal deletion, new protocol, key recovery,
brute-force flow or package ownership change is included.

## Compatibility / evidence boundaries

The two list modules now use their view enter/exit callback slots. No current
in-tree caller replaces these slots; an out-of-tree wrapper which does so can
bypass scrolling lifetime management and should use a parent container. Existing
signatures, FAP ABI and SDK exports are unchanged.

Our additional status ports were checked: power and clock changes already
request redraw; RPC virtual display does too. GUI add/remove/front/back,
lockdown, status visibility, direct-draw release and stream start retain forced
redraw. Companion1.11.33 uses RPC traffic/idle probing, not continuously changing
screen frames, for connection evidence. No iOS or FW Packages update is required
by these internal changes. These are source/host checks, not physical acceptance.

Native host fixtures execute actual C functions with fatal ASan/UBSan and
`-Wall -Wextra -Werror`. Check redraw coalescing/re-arm/timeout, lifecycle/timer
lock ownership, all BLE icon combinations, SD transitions, valid/invalid IR
and scratch cleanup. CI builds normal and diagnostic profiles in separate jobs;
the diagnostic job has read-only permissions and uploads only a small report.
It cannot flash a device, publish firmware or claim on-device test results.

Before any later Dev release, run full host tests, normal F7/resource/FAP gates,
the unchanged4096 C2/DfuSe validator and exact merged-version checks. For manual
acceptance: menus/Settings and IR button scrolling; app exit/reopen; BLE connect
and transfer icon on a static screen; SD error/recovery icon; display stream and
direct-draw resume; supported IR databases and both malformed test fixtures.
Use a test SD/backup and separate permission for diagnostic firmware installation.
No hardware tests or independent review are claimed by this source work.

## SDK audit gate

The separate JavaScript SDK audit also blocked this integration. Keep its
`moderate` threshold unchanged: no ignored advisories or registry-error bypass.
The documentation-only TypeDoc/theme move to development dependencies; scoped
overrides resolve minimatch11.0.4, brace-expansion5.0.12 and markdown-it14.3.3.
A checked-in TypeDoc adapter preserves the old partial-directory and negation
behavior against minimatch11, with real TypeDoc regression tests and a full
documentation build. Building repository documentation uses Node22; installed
SDK/wizard runtime support stays at Node>=18.12.

The wizard only replaces the literal `<app_name>` marker in a copied template.
Use a small binary-safe file walker instead of the generic replacement/glob
dependency chain; skip hidden entries and links. Its tests cover the real
template, literal paths/replacements, binary bytes and invalid roots. Existing
interactive prompts and installation behavior are not redesigned by this patch.
CI verifies packed candidates in fresh production consumers on Node18.12.0 and22,
so local development overrides cannot mask an incompatible published layout.
No npm publication, JS firmware runtime, new ABI export or device flash is added.
