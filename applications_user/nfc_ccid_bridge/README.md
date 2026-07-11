# NFC CCID Bridge

This app is the development proof for
[Tumoflip issue #63](https://github.com/squazaryu/tumoflip/issues/63). It exposes
an authorized physical ISO14443-4A card presented to Flipper as an app-local
USB CCID reader on macOS.

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
