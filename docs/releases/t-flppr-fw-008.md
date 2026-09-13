# Tumoflip Stable 008 (v1.0.8)

Promotes the Dev 008 line through 008-028, with two additional Unleashed 093
adaptations: reproducible source gathering and the Wisniowski2/Wisniowski1Rv
frame-field correction.

## Changes since Stable 007

- Build: preserve source-pattern order across Python hash seeds and apply
  built-in source exclusions per folder, including shared APP/STARTUP folders.
- Sub-GHz: retain separate Standard/Read RAW settings, the reviewed protocol
  additions from Dev 008, external-radio recovery and RAW-worker fixes.
  Complete the Wisniowski variant discriminator handling without replacing
  user or system keystore files.
- NFC: retain the Dev CUID iteration, transactional user-key dictionary import,
  and parse-popup cleanup fixes.
- Infrared: save a working Universal Remote signal to a new or existing remote.
- Desktop and GUI: promote the reviewed local layouts and quick-settings page,
  loading-screen lifecycle fixes, asset extraction progress, TextInput buffer
  correction and word-wrapped scrolling messages.
- Applications: Quac, Nearby Files and Weather Editor remain independently
  delivered through FW Packages; Weather Editor uses the native UI revisions
  and guarded save/load behavior validated in Dev 008-028.

## Installation

Use the updater archive for t-flppr-fw-008. Back up important captures and
settings before updating from an older stable version. Firmware API is 88.7;
use compatible FW Packages and Community applications. No factory reset is
required. The companion's independent stable package catalog is updated
separately after the firmware assets have been verified.

Software CI, sanitizer and build validation do not imply physical acceptance
of every protocol or expansion board. Previously pending RF and external-radio
hardware checks remain pending; test them with your own devices.
