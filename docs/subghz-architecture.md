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

Desktop follows the same boundary:

- `Sub-GHz` opens the core firmware app;
- `ARF Tools` opens the ARF tools folder;
- `ARF Sub-GHz Full` is only a lightweight launcher, not a replacement for the
  core app.

Release validation and unit tests enforce this layout by rejecting stale
`arf_subghz_standard.fap` package entries and by checking that Desktop no longer
redirects `Sub-GHz` to ARF Full.
