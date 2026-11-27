## v1.10 (Tumoflip)

- Fit the actual filename on the loading and waveform screens, keeping both
  ends of long names visible without overlapping the duration.
- Keep the loading screen until the initial markers and overview are ready.
- Selective display-only adaptation inspired by Lechnio's RAW Edit v2.0
  in [Unleashed 26a9c8a6](https://github.com/DarkFlippers/unleashed-firmware/commit/26a9c8a62f00deb01d57345c5c36451d1dedac29).
  No changes to capture files, editor launch, decoding or radio behaviour.

## Post-v1.7 Tumoflip sync

- Replaced free-text merge settings with bounded NumberInput controls.
- Allowed a 0 ms manual merge gap without adding an artificial separator.

## v1.7

- Extended the configurable merge gap to 1-1000 ms.
- Preserved long RAW levels by splitting them into bounded same-level chunks.
- Kept native synchronization gaps between repeats of the same signal.
- Added checked sample-count arithmetic and peak-RAM validation for merges.
- Kept Tumoflip's receive/analysis-only policy and API 88 package routing.

## v1.6

- Fixed MPU fault stack overflow error on some flipper firmwares.
The bottle neck is internal deserializer called when loading recognized signals.
The change bumps the application stack from 2KB to 3KB as it seems sufficient value.

Note:
If you ever encounter this issue in the future please let me know on GitHub.
I don't want to raise this value too high if it's not needed because raising
the stack reduces the already very limited heap resources.

## v1.5

- Added merge feature for .sub files
- Added jitter normalizer option
- Added support for loading as RAW any other protocol supported by the firmware.
This was achieved by using the device's internal decoder.

## v1.4

- Added cut feature
- Added loading and saving screen views
- Added marks indicating there is more signal data on that side
- Reorganized overlapped views
- Extended max visible basename length to fit default RAW filename with its full date
- Optimized loading longer .sub files and implement limiter
- View will now automatically switch when swiched to invisible marker

## v1.3

- Apply proper icons for listed .sub files

## v1.2

- Made A/B markers recompute its position correctly in the waveform mode.
When the markers were aligned to a signal edge in the waveform mode,
its possition relative to that edge changed while zooming in/out.

## v1.1

- Adjusted namings
- Highlighted the selected A..B range with a dotted line along the top of the wave area
- Auto-number saved trims to avoid overwriting, show saved filename

## v1.0

- Allow editing RAW Sub-GHz files
