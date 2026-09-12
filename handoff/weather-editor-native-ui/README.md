# Weather Editor native UI review

These images execute production C draw callbacks with the repository's U8g2,
fonts and icon assets at 128x64. Display models contain controlled sample data.
They are host renders, not photographs or recordings from a Flipper.
The earlier `/tmp/weather-editor-preview.png` hand-painted mockup is obsolete.

- `after/comparison.png`: four identical scenarios before and after the patch.
- `after/all-screens.png`: 24 views/states, including extreme values, scrolled
  errors and the external-radio scanning screen.
- `before/all-screens.png`: baseline at `379655d24fb76ea139b405a24236416565eeff81`.
- Each directory contains native-size PGM frames, enlarged PNG frames and evidence.

Coverage includes both menu pages, editor pages, radio settings, unit settings,
saved source selection, file list and pagination, empty list, Simulation,
scanning, receiver list, normal/long station details, numeric input,
success/error widgets, About and hexadecimal Sensor ID input. Shared GUI draw
functions are executed directly; no alternative Pillow text/layout renderer is
used inside the LCD. Pillow only scales pixels and labels the contact sheets.

The app-local VariableItemList preserves the upstream input behavior while
allocating its right column by rendered value width. Labels, values and arrows
use the same baseline. The native list never had the vertical misalignment
shown in the old manual mockup. Actual issues were clipped values and overlapping
fields on the sensor detail screen, plus the unused About button obscuring text.

The Sensor ID render exposed an existing TextInput stack overflow: inserting
the cursor needs another byte beyond text plus NUL. The firmware fix reserves
room for the cursor, ellipsis and terminator. AddressSanitizer reproduced the
baseline failure and passed after the fix, including every cursor position for
input lengths 0..79. The host adapter does not model firmware task scheduling.
Navigation checks execute the list's actual up/down/left/right functions.

Reproduce on macOS using the repository toolchain Python:

```sh
./toolchain/arm64-darwin/bin/python3 tools/tumoflip/render_weather_native.py \
  handoff/weather-editor-native-ui/before \
  --ref 379655d24fb76ea139b405a24236416565eeff81 --skip-text-input
./toolchain/arm64-darwin/bin/python3 tools/tumoflip/render_weather_native.py \
  handoff/weather-editor-native-ui/after --sanitize \
  --compare handoff/weather-editor-native-ui/before
```

FW Packages dev-015 is already published and must not be replaced. The older
local weather dev-015 candidates from this conversation are invalid release
candidates: their control checkout was stale. Use the live dev-016 successor
and current predecessor contracts for future packaging. This UI work does not
publish or install a package. The common TextInput fix belongs to the firmware.
