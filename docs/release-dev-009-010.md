# Tumoflip Dev 009-010 — release candidate

## Changes

- **PSA/Fiat detection:** PSA AM accepts only its 80-bit frame or 88-bit aligned
  form. The trailing alignment bits do not enter the saved key; longer continuous
  Fiat V1 frames are not accepted as PSA. Fiat V1 advertises FM support in the
  ProtoPirate and RollJam implementations. The built-in Toyota protocol remains
  enabled. Renault V1 is not included because Tumoflip has no matching decoder.
- **Nord ICE:** normalize the two on-air frames into one decoded key, reproduce
  the pair when transmitting, allow the four button choices, and add the
  433.92 MHz AM650 entry to Add Manually.
- **NFC:** show per-entry CUID dictionary progress on the shared Loading view.
  The plain spinner and labeled/progress view remain separate instances so their
  state and animation lifecycle do not leak between operations.

F7 API remains **88.13**. No FW Packages API refresh is required for this candidate.

## Installation and acceptance

This is a Dev build, not a stable release. Back up `/int` and important SD files
before flashing. Manual hardware acceptance remains pending:

1. On owned/test Fiat V1 hardware, check FM capture recognition and verify a Fiat
   recording is not listed as PSA. Also check PSA AM recordings that contain 80
   and 88 decoded bits.
2. With an owned/test Nord ICE remote, capture one button, confirm the saved entry
   is unique, then test pair transmission and the three alternate button choices.
3. Scan a large CUID dictionary, confirm progress advances during the index pass,
   then leave the screen and verify the loading animation stops cleanly.

Host build and release validation do not replace these device checks.
