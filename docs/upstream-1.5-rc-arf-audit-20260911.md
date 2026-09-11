# Upstream RC and ARF audit — 2026-09-11

## Official Flipper firmware

The official `1.5.0-rc` build is based on `1.4.3` at commit `597f5549`. Its
release notes cover 50 commits across 151 files. The review found that the
useful protocol, NFC, date/time, storage, GUI, HID, and CCID changes are
already represented in Tumoflip's current tree. The only selected adaptation
for Dev 008-027 is the small public `view_dispatcher_check_id()` API from
official PR #4422.

The official hotel parser was intentionally not copied: its verify/read
callbacks are stubs and it accesses sector trailers without the null/error
guards required by this firmware. A future implementation must remain a
read-only plugin with bounded parsing and explicit hardware acceptance.

## ARF

The latest ARF Dev releases were inspected in order:

- `dev-51d4700d`: broad protocol-output rewrite and ChiefCooker hopping UI;
  it also disables Toyota in the registry and removes the PSA2 brute-force
  routing. Excluded.
- `dev-892092a1`: restores button labels and Hitag2 display text only; it does
  not restore the regressions above. Excluded.
- `dev-51efff55`: optimizes Hitag2 Hell and adds an optional BLE offload path,
  but commits `subghz_hitag2_hell.o` and has no phone-side implementation in
  the repository. Excluded pending an isolated design, protocol, and
  hardware/phone validation.

No ARF source is cherry-picked by this release. Standard Sub-GHz and the
integrated ARF path therefore retain the existing Toyota/VAG/ProtoPirate
behavior.
