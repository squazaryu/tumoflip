# Sub-GHz Architecture

Tumoflip keeps the stock `Sub-GHz` application as the primary receiver,
transmitter, saved-file, RAW, radio settings, external CC1101, RPC, and
file-launch surface, but ships it as an SD `.fap` instead of embedding the full
UI in C1 firmware flash. This recovers firmware/C2 headroom while preserving
the standard Sub-GHz behavior behind the ARF hub.

This is intentionally different from the removed `arf_subghz_standard.fap`
experiment. There is still only one stock Sub-GHz implementation; it lives at
`/ext/apps/Sub-GHz/subghz.fap`. `arf_subghz_standard.fap` must not be
reintroduced as a second copy.

The supported extension model is:

- stock Sub-GHz as `/ext/apps/Sub-GHz/subghz.fap` for normal workflows;
- `.fal` Protocol Packs under `/ext/apps_data/subghz/plugins` for optional
  decoders;
- isolated ARF utilities as separate `.fap` processes under
  `/ext/apps_data/arf_subghz_full/modules`;
- visible ARF entry points under `/ext/apps/ARF Tools`.

Desktop uses Tumoflip shortcuts on top of the same runtime boundary:

- `Sub-GHz` opens `ARF Sub-GHz Full`, the Tumoflip Sub-GHz hub;
- `ARF Tools` opens `Module One Cockpit`;
- `Standard Sub-GHz` inside `ARF Sub-GHz Full` opens
  `/ext/apps/Sub-GHz/subghz.fap`;
- the ARF tools folder remains available under Apps.

Release validation and unit tests enforce this layout by rejecting stale
`arf_subghz_standard.fap` package entries and by checking that the hub routes to
the single stock Sub-GHz FAP instead of shipping a second standard Sub-GHz copy.

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

- 75 common paths between core Sub-GHz and ARF FAP sources;
- 33 byte-identical shared files tracked by
  `tools/tumoflip/subghz_drift_manifest.txt`;
- 42 intentionally diverged files.

The remaining diverged files include ARF profile adapters, app entry points,
radio lifecycle/state code, and ARF-only UI scenes. The ARF Frequency Analyzer
view is also intentionally forked because the standalone ARF app adds
receive-only field notebook export behavior to the analyzer OK action. Those
files must not be shared mechanically. Extracting them requires a small explicit
API first, plus FAP size, heap, and launch/exit validation.

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
