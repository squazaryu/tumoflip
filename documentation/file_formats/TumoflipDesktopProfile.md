# Tumoflip Desktop Profile

Tumoflip Desktop Profile selects a collection of existing Dolphin animations without modifying
`/ext/dolphin/manifest.txt`.

The profile is stored at:

```text
/ext/apps_data/tumoflip_customization/desktop_profile.txt
```

## Format

```text
Filetype: Tumoflip Desktop Profile
Version: 2
Enabled: true
Collection: Favorites
Order: Sequential
Timing: Custom
Duration: 90
Selection: Explicit
Animation: L1_Tv_128x47
Animation: L1_Cat_128x47
```

Fields:

- `Enabled`: enables or disables collection filtering.
- `Collection`: non-empty UTF-8 display name up to 64 bytes, without control characters.
- `Order`: `Random` or `Sequential`.
- `Timing`: `Original` or `Custom`.
- `Duration`: custom duration in seconds from 5 through 86399. It is retained when `Timing` is
  `Original` so clients can switch modes without losing the chosen value.
- `Selection`: `Explicit` uses the repeated `Animation` fields. `All` selects every available
  animation and must not contain `Animation` fields.
- `Animation`: repeated manifest animation name. Names may contain ASCII letters, numbers,
  underscores, and hyphens. An explicit profile supports up to 320 unique names.

Version 1 profiles remain supported and are interpreted as `Selection: Explicit`.

Missing, malformed, or empty enabled profiles are ignored and the normal manifest selection is
used. System animations for an unhealthy battery or a missing SD card still keep their normal
guards.

## Applying a profile

Clients must use a staged write:

1. Write and close `desktop_profile.txt.tmp` in the same directory.
2. Verify the staged bytes, remove the previous profile, and rename the temporary file to
   `desktop_profile.txt`.
3. Create and close `/ext/apps_data/tumoflip_customization/reload.flag`.

Desktop consumes `reload.flag` and reloads the profile in its own event loop. This avoids changing
animation state from the storage worker and prevents partially written profiles from becoming
active.

## Companion-installed animation packs

Companion-managed packs keep their manifest entries separate from the stock manifest:

```text
/ext/apps_data/tumoflip_customization/animation_packs.txt
```

The file uses the standard `Flipper Animation Manifest` format. Desktop reads the stock manifest
first and then appends unique entries from the Companion manifest. Animation frame and metadata
directories remain under `/ext/dolphin/<animation-name>/`. Missing or malformed Companion
manifests are ignored without changing the stock animation list.
