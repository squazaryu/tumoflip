# Desktop menu hardening

Selective adaptation of [Unleashed d0d7cce590 / PR #1121](https://github.com/DarkFlippers/unleashed-firmware/pull/1121)
on top of Tumoflip `t-dev-008-013`. This does not import the upstream plugin-style
menu rewrite tracked in [#445](https://github.com/squazaryu/tumoflip/issues/445).

## Scope

- Side List and Side Grid leave the canvas vertical until `canvas_commit()`
  sends the framebuffer callback. The next GUI viewport resets orientation
  before setting frame geometry and drawing. LCD draw calls, menu navigation
  and existing left-handed behavior are unchanged.
- Every loaded favorite shortcut path has a bounded NUL terminator. Legacy
  v14/v17/v18/v19/v20 settings are sanitized before the existing migration save;
  current v21 settings are sanitized in memory without a new disk write.
  Valid shortcut names, all other settings, schema version and defaults remain
  unchanged. This is not a transactional rewrite of `saved_struct` storage.
- No new layouts, style plugin loader, API bump, launcher route change, package
  replacement or modification to upstream monitoring is included.
- The upstream scroll-timer/model-lock deadlock does not map to the current
  Tumoflip menu, which has no scroll timer. No speculative timer/reset change
  is included in this adaptation.

## Automated verification

Run from the exact worktree using its firmware-toolchain Python:

```sh
python3 -m unittest \
  tools.tumoflip.test_main_menu_layouts \
  tools.tumoflip.test_menu_stream_orientation \
  tools.tumoflip.test_desktop_settings_strings \
  tools.tumoflip.test_desktop_favorites \
  tools.tumoflip.test_ci_workflows
python3 tools/tumoflip/test_desktop_settings_strings.py --coverage
```

The stream test host-compiles the production rotated draw functions and
`canvas_commit`, with rendering primitives faked, for empty menus and every
position in menus of 1 through 40 entries. Source guards check GUI reset ordering.
It does not execute GUI scheduling, draw real LCD pixels or exercise the RPC
protobuf transport. The settings suite compiles the real loader/migrations and
`saved_struct` with memory-backed storage and genuine file headers/checksums.
It covers all six supported versions, valid/nonterminated strings, corrupt or
missing files and migration write failures. Both suites run in PR/release CI.

## Device acceptance — not run

After installation of a future build containing this change:

1. Select Side List, then Side Grid. Stream the screen through qFlipper or
   TumoCompanion: the received orientation must match the rotated menu.
2. Exit each rotated layout to Desktop, Settings and an app. Confirm the next
   screen is not rotated. Repeat with left-handed mode enabled.
3. Recheck Up/Down/Left/Right, OK and Back in all existing layouts, including
   no-op directions in Rail and the last partial row in Wii.
4. Check all five favorite shortcuts, reboot, and confirm their targets remain
   intact. Legacy migration tests must use backed-up disposable settings, never
   deliberately corrupt the user's only settings file.

Build/host-test success is not hardware acceptance. This integration itself
does not publish, install or replace a firmware release.
