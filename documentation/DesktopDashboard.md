# Classic / Panel desktop modes

Implemented locally on 26 November 2026. Code checked at
`cca74f17b173272b38d8949db4a7ea8156049ff1` on
`feat/core-activity-idle-recovery`. No push, PR, release, catalogue activation,
device connection, installation or flashing was performed.

## Use and compatibility

In the updated **Settings → Desktop**:

- **Desktop view:** `Classic` keeps the existing wallpaper Desktop; `Panel`
  displays Activity, Idle & sleep and Recovery as home pages.
- **Edit shortcuts:** independently chooses the Classic or Panel binding set.
  Editing one set never overwrites the other. Changing Desktop view selects
  its editor initially, but another set can be edited without changing the mode.
- **Panel motion:** On/Off. All panel changes are saved when leaving Settings.

| Input | Classic | Panel |
| --- | --- | --- |
| Short Left / Right | Existing shortcuts / interaction | Previous / next page |
| Held Left / Right | Existing held shortcuts | Independent Panel app targets |
| Held OK | Existing target | Independent Panel target |
| Short OK | App menu | App menu |

The existing Debug-mode held-OK override is unchanged. Up, Down, lock, archive
and power-off gestures retain their original behaviour. Home pages are read-only:
Pause/Restart confirmations remain on the separate system activity screen,
not on home-page OK. A held or repeating direction never also changes the page.

This is a core feature, not a new FAP. The existing standard Desktop Settings
FAP was adapted; its category, provenance and install identity are unchanged.
Delivery needs the new core plus updated `desktop_settings.fap` in firmware
resources/FW Packages. No TumoCompanion code or SDK API change is required.
API remains **88.23**. On an older core without the private versioned capability
record, the new Settings FAP retains its legacy controls and row routing and
hides unsupported Panel options. It does not hard-code a firmware release tag.

## Persistence and runtime bounds

The public DesktopSettings v21 layout remains **648 bytes**. Existing wallpaper
preferences, app paths, defaults and PIN data are not migrated or rewritten into
the new format. On first use, the three Panel held targets are copied in RAM
from the existing held Left/Right/OK targets; subsequent edits are independent.

The private extension uses `/int/.desktop_panel.0` and `.1`: versioned
SavedStruct records with CRC32, generation, mode, motion and three bounded
128-byte targets. Missing/invalid loads do not write defaults. The previous
validated bank is retained while the other is written and read back. Partial
writes, corrupt/oversized records, malformed strings and stale-generation
updates are rejected. Retrying an identical complete write confirms it without
another write. Failed verification is reported as unconfirmed, with Retry/Close,
not silently presented as success.

The wallpaper manager retains its existing frame ownership and app handoff;
deferred retained-frame work (#228) is not resumed. Panel mode pauses wallpaper
decoration and swaps only Main's background slot when Main is unlocked and owns
the view callbacks. The locked stack never displays Panel information. This
does not claim to free the existing wallpaper frame cache from RAM.

The incoming-page transition is bounded to 160 ms. Stock clock/loading icons
update at 4 Hz only when applicable. One timer posts to Desktop's existing
coalescing non-blocking queue; it does not render or allocate in the timer task.
No additional 128×64 screen buffer is allocated. Motion is cancelled while
hidden, locked, in another app/menu, with backlight off, in recovery or with
Panel motion Off. File operations/transfers are not suspended.

## Build-size integration

The first normal build with this feature exceeded the radio boundary. The
former LTO setting covered app sources but not the resident libraries.

Resident libraries now use fat LTO objects: optimizer IR for C1 and native code
for SDK/plugin links. FAP builds explicitly use `-fno-lto`; the compiled Settings
FAP has normal symbols and no `.gnu.lto` payload. Updater-library policy remains
unchanged. Existing exports are retained and no function was removed.

Three translation units remain native: crash register capture (`check.c`),
newlib allocator wrapping (`memmgr.c`) and the F7 startup/vector table.
Experiments exposed and rejected out-of-range literal loads, a lost allocator
wrapper and a dropped startup vector table. No hand-written assembly or
allocator behaviour was changed to bypass these failures. A new release
validation checks all 79 startup vectors, stack alignment and Thumb handlers.

## Exact local receipt

- Clean normal build (`BUILD_DIRTY 0`): `firmware_all fap_desktop_settings`.
- Full native discovery: **1478 tests passed**, 80.596 seconds; no skipped gate.
  Serial-start failure messages are expected fake-transport cases, not attempts
  to communicate with a device.
- Profile/model headers: **100% line and region coverage** with Clang/LLVM.
  Every partial record-write cut is exercised against actual SavedStruct I/O
  on synthetic storage; no real internal storage/SD is opened.
- **24** production activity/home/transition states and **6** Settings list
  states rendered with firmware fonts/icons and ASan/UBSan. Long values and
  settings captions are checked against native widget limits. The status bar
  in previews is synthetic stock artwork, not a device observation.
- ARM fat archive tested as both LTO resident code and a normal native plugin;
  Settings FAP passes APPCHK at API 88.23.
- F7 binary: **875160 bytes**; text 694352, rodata 179748, data 724, BSS 7672.
- ELF and DfuSe end agree at **0x080D5A98**, with valid DfuSe CRC and a loadable
  **316-byte** startup vector table. Erase ends at **0x080D6000**; radio starts
  **0x080D7000**. Section gap **5480 bytes**, physical reserve **4096 bytes**:
  the unchanged 4 KiB minimum passes.
- The earlier partial-LTO feature build was 881992 bytes; this integration is
  6832 bytes smaller despite retaining the functionality. BSS is not a measure
  of total heap use. Physical timing/heap acceptance has not been performed.
- Settings FAP: **42524 bytes**, 3 KiB declared stack. Diagnostic stack output
  measured 1224 bytes for store and 432 for bank selection before nested storage
  callers, so the previous 1/2 KiB budgets were not retained.

```text
firmware.bin        21a24a66160910930716baa9b561a9e9732d430eed548a059255550b7836fe48
firmware.dfu        52b00a9fdee9bdf8db3d3ead864e5f8aec5b0c6f50d3567f4e7db2e1b08d12fd
desktop_settings.fap 4041af3ef024f2613bab841c89552541cdce202ed485c4d1c2b979ed9ef49f0b
```

No updater/resources/full FW Package acceptance or independent/hardware review
is claimed. The local default version string still says `t-dev-011-004`; these
are **not** replacement assets for that published release. A separately
authorized release needs a fresh version, exact-source CI, normal package
validation and remote checksum verification. Hardware acceptance remains open.

Rebuild with the bundled toolchain:

```sh
FBT_NO_SYNC=1 ./fbt -j2 firmware_all fap_desktop_settings
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=.:tools/tumoflip \
  toolchain/current/bin/python3 -m unittest discover -s tools/tumoflip -p 'test_*.py'
PYTHONDONTWRITEBYTECODE=1 toolchain/current/bin/python3 -m tools.tumoflip.test_desktop_panel --coverage
```
