# NFC CCID Bridge

This app is the development proof for
[Tumoflip issue #63](https://github.com/squazaryu/tumoflip/issues/63). It exposes
an authorized physical ISO14443-4A card presented to Flipper as an app-local
USB CCID reader on macOS.

The product host for this transport is
[TumoCard Studio](https://github.com/squazaryu/tumocard-studio). It performs
automatic PC/SC discovery, bounded public AID inspection, an auditable APDU
timeline, and redacted report export. Its client-side policy accepts only
SELECT, READ BINARY, GET RESPONSE, and GET DATA instructions; state-changing
APDUs are rejected before PC/SC transmission.

The USB worker and NFC poller communicate through a bounded mailbox. NFC radio
operations only run inside the NFC callback. APDU payloads are never written to
logs or storage; the screen retains only header metadata, length, result, and
status word.

## Safety modes

- The app starts in `READ` mode. Only SELECT, READ BINARY, GET RESPONSE, and GET
  DATA instructions are relayed.
- Hold OK to arm `FULL` relay for the current app session.
- Press OK to return immediately to `READ` mode.
- Removing the card, a timeout, malformed APDU, or Back fails closed.
- Back requests a stop from the NFC callback before the UI exits, avoiding a
  blocking poller join when a non-ISO14443-4A card is still in the field.
- The screen distinguishes scanning, an activated card, APDU relay, and exit
  states so card activation is not mistaken for an application freeze.
- An activated card waits briefly between mailbox checks so the NFC worker does
  not starve the GUI while no host APDU is pending.
- ISO14443-4A exchanges use the same extended wait/retry transport as the stock
  EMV poller. `PROTO` and `TIMEOUT` describe bridge transport failures; the
  synthetic `SW:6400` is not a status word returned by the physical card.
- Debounced antenna-amplitude sensing detects physical card removal without
  resetting an active APDU session. The UI returns from `Card ready` to
  `Scanning for card` and notifies the USB host. Presence sampling is armed only
  after the first successful APDU, then runs during idle periods and consumes
  its diagnostic IRQ locally.
- Right opens a three-page About screen with supported card families, explicit
  limits, app version, and the project GitHub address. Back and About use button
  glyphs instead of plain command text.

`FULL` mode is intended only for cards and commands the user is authorized to
test. It does not bypass authentication or card cryptography.

## Hardware acceptance

1. Launch `Apps -> Module One -> NFC -> NFC CCID Bridge`.
2. Present an ISO14443-4A development card and verify `CARD:READY USB:ON`.
3. Confirm macOS PC/SC lists `Generic USB Smart Card Reader`.
4. Run repeated SELECT and read-only APDUs from a host tool.
5. Remove and re-present the card; PC/SC must recover without restarting Flipper.
6. Exit with Back and verify qFlipper, stock NFC, and USB SD Mode still work.

The temporary PoC USB identity is `076B:3A21` so the macOS in-box CCID driver
binds without a custom driver. It is not a stable Tumoflip product identity.

## Supported card families

The bridge relays ISO14443-4A APDUs for authorized cards such as EMV payment
cards, MIFARE DESFire, MIFARE Plus SL3, NFC Forum Type 4 tags, JavaCard, and PIV.
Authentication and card cryptography still apply. MIFARE Classic and Ultralight
are not ISO14443-4A and are not supported by this bridge. The app is a relay, not
an automatic card decoder.
