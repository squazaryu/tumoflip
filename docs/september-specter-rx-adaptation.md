# Specter and RX adaptation, September 22

Base: published t-dev-009-006, 756187d03e. F7 API remains 88.12.

## Specter FW Package 3.2.0-tumo

Selected functional changes from author commit
9282dc39eb7146cf5ec6a16372e5ce4bb3bb36f0 (Specter 3.0.1):
survey and fingerprint spacing, no reader ring during calibration, TOO SHORT
for empty surveys below 10 seconds, no completion chime for that result, and
word-wrapped text log entries compatible with filtering. CSV stays one row per
finding. Missing survey data is also TOO SHORT locally.

Keep Tumoflip's sync/close checks, capacity preflight, short-write rollback and
distinct full/SD-error status. The package-only route and protected ownership
are unchanged. Local version 3.2 is deliberate: FAP metadata only stores two
version components, and 3.1 already identified our previous adaptation.

Imported test_survey.c and test_logwrap.c are from the same author commit,
under the existing app license. The NULL-result expectation is adapted to our
missing-evidence behavior. Both run with address/undefined sanitizers through
test_specter_package.py; existing SD failure-injection tests remain enabled.

Native preview:

    toolchain/current/bin/python3 -m tools.tumoflip.render_specter_native /tmp/specter-ui

This compiles production draw callbacks with firmware U8g2 fonts and renders
16 states including errors. It does not simulate NFC acquisition or LCD timing.

## Shared reception profiles

all-the-plugins a80f75c6705bd709d0c6f922e84eb940f99730fc adds Honda 315 MHz
beside Honda 433.92 MHz. Add it at the end of our bounded table, preserving
existing indices and the 009-006 generic-modulation filtering. Both Honda
profiles register the same owned preset by identity, without a hardcoded index.
The shared table serves Standard/Read RAW and the customized ProtoPirate.
No dynamic model database or upstream Config-plugin layout is imported.

ARF 32b36f623e566bff631ce7307be4f632eafc849b adds AM eligibility to PSA/PSA2.
Only that metadata change is selected. Existing decoders, encoders, frequencies
and persisted-file formats are unchanged. Tests feed pulses through the actual
receiver dispatch with production descriptor flags for AM, FM and RAW filters;
this proves routing eligibility, not reception of an unprovided physical capture.

## Delivery and device checks

Firmware and affected FW Packages must be built from this source for delivery:
Specter, ProtoPirate (embedded Honda table), and PSA protocol plugin(s).
This document does not assert publication or physical acceptance.

On device: verify both Honda choices, profile switching and independent
Standard/Read RAW persistence; test known owner-provided AM and FM captures.
For Specter, test short/complete surveys, positive detection during a short
survey, calibration in a reader field, log wrapping/filtering and SD errors.
