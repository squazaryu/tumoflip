# Sub-GHz Architecture

Tumoflip keeps the system `Sub-GHz` application in firmware as the primary
receiver, transmitter, saved-file, RAW, radio settings, external CC1101, RPC,
and file-launch surface.

This is intentional. Hardware testing showed that rebuilding the full standard
Sub-GHz app as an external `.fap` creates a second large Sub-GHz copy and can
exhaust the Flipper application heap. The failed external backend was removed;
`arf_subghz_standard.fap` must not be reintroduced as a release module unless
Sub-GHz is first split into a smaller service/client architecture and validated
on hardware.

The supported extension model is:

- core Sub-GHz in firmware for normal workflows;
- `.fal` Protocol Packs under `/ext/apps_data/subghz/plugins` for optional
  decoders;
- isolated ARF utilities as separate `.fap` processes under
  `/ext/apps_data/arf_subghz_full/modules`;
- visible ARF entry points under `/ext/apps/ARF Tools`.

Desktop uses Tumoflip shortcuts on top of the same runtime boundary:

- `Sub-GHz` opens `ARF Sub-GHz Full`, the Tumoflip Sub-GHz hub;
- `ARF Tools` opens `Module One Cockpit`;
- `Standard Sub-GHz` inside `ARF Sub-GHz Full` opens the core firmware app;
- the ARF tools folder remains available under Apps.

Release validation and unit tests enforce this layout by rejecting stale
`arf_subghz_standard.fap` package entries and by checking that the hub keeps
standard Sub-GHz as a launch target instead of shipping a second standard
Sub-GHz copy.

The ARF FAPs still share some source files with the core Sub-GHz app. Files
listed in `tools/tumoflip/subghz_drift_manifest.txt` are expected to remain
byte-identical between `applications/main/subghz` and
`applications_user/arf_subghz_full`. The release test
`tools/tumoflip/test_subghz_drift.py` runs `check_subghz_drift.py` and fails if
one of those shared files changes in only one copy. Files outside that manifest
are treated as intentional ARF profile forks until they are extracted behind a
smaller shared API.

## Current Shared Boundary

The first cleanup pass keeps behavior unchanged and removes false divergence:
files that differed only by comment whitespace were synchronized and added to
the drift manifest. The current checked surface is:

- 80 common paths between core Sub-GHz and ARF FAP sources;
- 34 byte-identical shared files tracked by
  `tools/tumoflip/subghz_drift_manifest.txt`;
- 44 intentionally diverged files.

The remaining diverged files include ARF profile adapters, app entry points,
radio lifecycle/state code, and ARF-only UI scenes. The ARF Frequency Analyzer
view is also intentionally forked because the standalone ARF app adds
receive-only field notebook export behavior to the analyzer OK action. Those
files must not be shared mechanically. Extracting them requires a small explicit
API first, plus FAP size, heap, and launch/exit validation.

Core `Sub-GHz` radio settings intentionally diverge for issue #92. The core app
keeps module selection separate from `RX Mode: AUTO/DUAL` and provides a direct
AUTO/DUAL toggle in the Read screen. Dual receive runs through the Radio Broker
lifecycle, while the ARF FAP keeps its profile-specific `subghz_txrx`
implementation. `Standard Sub-GHz` in the ARF Hub launches the core firmware
app, so diversity remains available from the supported user path without
duplicating the core receiver inside a FAP. The core receiver view header also
diverges to update a deduplicated history row when the second radio reports a
stronger copy of the same frame.

RAW Auto Decode is intentionally core-only. The core `More RAW` and `Decode RAW`
scenes can temporarily switch the firmware-owned Protocol Pack registry,
scan one group at a time without using the radio, and restore the user's active
group on exit. The ARF profile has its own registry and advanced tool lifecycle,
so mechanically copying these scenes would blur the supported boundary and
duplicate the same primary workflow. These two scene files are therefore
excluded from the byte-identical drift manifest; their radio-free RAW worker
contract remains covered by the shared lifecycle tests.

## Shared APIs

`lib/subghz/subghz_hopper_plan.h` is the first explicit shared helper extracted
after the drift guard. It is a header-only, radio-free planner for the next
frequency/preset hopping indexes. The helper is used by the core Sub-GHz
combined hopper and by the ARF combined hopper, while each app keeps its own RSSI
dwell timing, radio reset/reload sequence, settings UI, and profile-specific
state.

This boundary is deliberate: pure index calculation can be shared safely, but
radio lifecycle code and scene navigation stay local until they have a smaller
service/client API and hardware validation.
