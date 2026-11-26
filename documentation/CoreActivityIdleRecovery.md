# Core activity, quiet idle and crash-loop recovery

Local implementation, 26 November 2026. Not published or installed on a device.
These are core services/Desktop changes, not a new FAP. Public SDK API remains
88.23; existing FW Packages and TumoCompanion do not need a new API.

## Controls

On the unlocked Desktop, open the existing lock/quick-settings menu. From the
Brightness / Volume / Vibro page, press Right to open **System activity**.
Left/Right page between activity, idle/sleep and recovery. Back returns to the
Desktop; Left from the first page returns to quick settings. Slider editing
keeps its existing arrow behaviour.

- **Activity:** open writing file handles/metadata operations, reported phone
  transfer, and preview counts P/H (Bluetooth phone / other RPC host).
- **Idle & sleep:** whether Desktop decoration is paused, the actual HAL sleep
  hold count, and foreground-app state. The count is not an attribution of
  unknown holds to individual apps. Existing auto-lock/auto-poweroff settings
  are unchanged.
- **Recovery:** this boot's early-crash count and boot mode. No automatic reset,
  SD cleanup, file enumeration, telemetry or settings migration.

**Pause preview** requires a second OK and explicitly warns that Bluetooth
will disconnect. It uses normal BT/RPC teardown rather than joining a blocked
transport worker from Desktop. The operation is denied while a foreground app,
reported transfer or writing/mutating storage operation is active. An atomic
barrier prevents new writers racing the disconnect. After completion **Allow
preview** re-enables new phone preview requests; it does not reconnect the
phone itself. Other RPC hosts remain separately counted.

## Quiet idle

When backlight is off and no RPC preview is active, pause frame playback,
animation-selection/profile debounce and Desktop clock timers. Preserve the
current frame, parsed animation/profile, deferred profile reload, and remaining
idle-animation duration. Wake/preview activity resumes the appropriate visible
timers. Hidden animation views cannot restart their timer behind a menu/app.

The normal Desktop/lock heartbeat drops from 500 ms to 5 s; other menus keep
their normal cadence. Producers signal a coalescing, non-blocking one-element
queue; no Storage/Notification producer waits on Desktop. File operations,
RPC, background notifications, input, PIN checks and active applications are
not suspended. This does not change retained-frame clone/cache/handoff (#228).

## Recovery

Three recorded crashes without a 60-second healthy boot select recovery on the
next boot. A redundant byte in previously unused crash-record metadata bits is
stored in the existing twenty RTC registers. No extra register, flash/SD write,
allocation, scheduler operation or callback is added to the fatal serializer.
The byte is independently checked and atomically clearable without invalidating
the retained OOM report. Acknowledging a crash report cannot reset the guard.
New code reads old records; an older firmware reader may reject a new internal
RTC record after a downgrade. The RPC crash-report format remains unchanged.

Recovery uses a static embedded animation, skips SD animation profiles and
blocks optional App Bridge `hello` / `fabric_open` / `fabric_step` actions.
Status/capabilities/crash reports, cancellation, transfers, normal storage,
manual applications and ordinary Bluetooth services remain available. This
mitigates a crash loop; it does not diagnose or fix every possible OOM cause.

The recovery controls appear only after the normal PIN unlock and after any
higher-priority boot screens. **Restart** requires a second OK, rejects active
writers/foreground work and clears only the loop guard/legacy popup pointer.
The last-crash report and user data are retained. Stable uptime clears the
persistent streak but keeps this boot in recovery until an explicit restart.

## Local checks and limits

- Synthetic host tests execute the actual policy wrapper, serializer, storage
  open/close/free and mutation dispatchers, RPC start/stop, animation timer
  gates, animation selection and Desktop recovery/PIN routing.
- ASan/UBSan cover counters, saturation/underflow, failure cleanup, deadlines
  across wraparound, normal/phone/host previews, and restart admission.
- Ten production GUI/input states render with firmware fonts/icons at 128x64,
  including maximum counters, denied actions and both confirmations. Text bounds
  and label/value separation are asserted. This is not physical LCD acceptance.
- The initial full host run found two stale test harnesses (the new quick-menu
  event and previously added Field Tools pending helpers). Both harnesses were
  updated without dropping their original assertions; their 25 tests pass.
- F7 normal LTO build compiles with API 88.23. Initial `-Os` candidate:
  880560 binary bytes; ELF/DFU C1 end `0x080D6FB0`, radio start `0x080D7000`.
  Section gap 80 bytes, erase-aligned gap **0**, below the release workflow's
  unchanged 4096-byte minimum. It must not be published as release-ready.

The same checkout was also built at the pre-feature source `532eb8ea1c`
without altering that branch: 876512 binary bytes, 4128 section-gap bytes,
4096 erase-aligned gap bytes and 7624 BSS bytes. The initial feature version
therefore adds 4048 flash bytes and 40 static-RAM bytes. Dynamic View/queue
allocations require later hardware measurement; they are not included in BSS.

Two isolated compiler experiments (`-Oz`, then `-Oz` plus
`-fno-inline-small-functions` at LTO link) produced the same section sizes as
the feature `-Os` build. They do **not** constitute a size fix. Their option,
build-script changes and temporary flag-contract tests were removed; normal
compiler flags, FAP/updater policy and the 4096-byte release gate are unchanged.
The functional work is implemented, but another measured size reduction is
needed before a release candidate can pass all gates. No installable release
is claimed by this document.

No hardware was connected/tested, installed or flashed; no external branch,
PR, issue, tag, catalogue, release or simulator was changed by this task.

## Native UI revision

Activity now uses three stock-icon badges instead of a text table: Files
(writers/mutations), Phone (reported transfer) and Screen (phone/host previews).
Short values use the standard bold font; long preview pairs use the existing
5x7 numeric font so `255/255` fits without clipping. Idle and recovery use
static native clock/face/warning assets and stock confirmation buttons. The
busy dialog's OK only dismisses that dialog; it does not arm another action.
All ten synthetic input/render states are checked for text/caption bounds.
The preview adds stock status-bar artwork, not a live device observation.

These pages currently remain separate system screens, **not** a replacement
for Desktop wallpaper. The later proposed Wallpaper / Dashboard selection,
home-screen left/right paging and lightweight transitions are not implemented
in this change. Such a mode must preserve the normal wallpaper alternative,
define the changed short-left/right gestures, stop decorative timers while
idle, and pass the same flash/RAM and PIN/transfer checks before publication.

## Final checked source and receipt

Code commit `0d63992c5d58e5d0c470d82c9e258ab5fcc2790e`, clean build
(`BUILD_DIRTY 0`), normal `-Os` / LTO flags, API 88.23:

- Full native host discovery: **1459 tests, all passed**, 70.436 seconds.
  The serial-start timeout messages are expected synthetic negative cases,
  not attempts to open a connected Flipper.
- All ten native-style GUI/input states passed ASan/UBSan and text bounds.
- F7 compilation succeeded: text 702588, rodata 176880, data 736, BSS 7664
  bytes. Binary length 880544 bytes, +4032 versus the measured pre-feature
  source. Shared policy transactions remove duplicated bookkeeping without
  changing the tested admission/notification behaviour.
- DfuSe format/CRC and C1 end match the ELF: `0x080D6FA0`.
  Radio begins at `0x080D7000`; section gap 96 bytes, erase-aligned end
  `0x080D7000`, physical spare page count **zero**.
- Strict release-layout check with **4096** required bytes fails as expected.
  The negative DfuSe-container gap (-213) includes non-flashed metadata; it
  is not a C1/radio overlap. No minimum was lowered and no failure was hidden.
- No updater/resources archive, complete package acceptance, independent review
  or hardware acceptance was performed. The build still carries the existing
  development default version string; it is not a new `011-004` release.

Diagnostic hashes before generated-output cleanup:

```text
firmware.bin dce3dea61b88435bbf521965e0684fa64d304ecc69961226ab938c4ec6e199d9
firmware.dfu f0c9207635619600fe9d8e91ad1913680de60eb7c92b0ba215e96461c56ae560
```

Only the code and compact GUI preview are to be retained; generated build
outputs can be reproduced from the checked commit with
`FBT_NO_SYNC=1 ./fbt -j2 firmware_all`. They must pass the unchanged release
gate before any installation/publication, including any future Dashboard mode.

Cleanup completed after the checks: removed the task-created
`build/f7-firmware-C` tree (129252 KiB allocated before cleanup), including
objects, libraries, ELF/BIN/DFU/HEX, map, SDK-header copies and compilation DB,
and its generated `build/latest` symlink. No build process was using it and no
tracked file was under that tree. Source, toolchain, previews, all other
branches, user files and SD data were preserved. The filesystem's free space
increased by 138032 KiB over the cleanup interval; that net figure also includes
unrelated background filesystem activity. Rebuild from the checked source using
the command above; no binary backup/archive was made.

Run focused checks:

```sh
PYTHONDONTWRITEBYTECODE=1 toolchain/current/bin/python3 -m unittest \
  tools.tumoflip.test_system_policy \
  tools.tumoflip.test_system_storage_activity \
  tools.tumoflip.test_system_integrations \
  tools.tumoflip.test_system_activity_layout
```
