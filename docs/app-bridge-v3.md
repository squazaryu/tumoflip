# Tumoflip App Bridge v3 Sessions

App Bridge v3 is a session protocol carried inside existing `FAB2`
`runtime` commands. It does not introduce a new GATT service, characteristic,
or frame magic, so `FAB1` and `FAB2` clients continue to work unchanged.

The v3 layer makes bridge ownership explicit for companion tools. Firmware does
not guess whether iPhone or Mac should win a BLE connection and does not
disconnect a valid central automatically.

## Payload Format

Runtime v3 payloads are UTF-8 key/value fields:

```text
key=value;key=value
```

Keys and token values are ASCII. Token values may contain letters, digits,
underscore, dash, and dot. Owner IDs are limited to 24 bytes.

## Commands

All commands use app ID `runtime` over `FAB2`.

### `hello`

Request:

```text
owner=iphone
```

Fields:

- `owner`: required stable owner ID, for example `iphone` or `macbook`.

Response command: `hello`

```text
sid=1234ABCD
```

## Errors

Invalid commands use the existing `runtime/error` response and set the FAB2
error flag. Current v3 errors are:

- `invalid_owner`
- `payload_too_large`
- `unsupported_command`

## Runtime Status

`runtime/status` keeps schema `2` and adds compact `sid=<session id>` and
`bo=<owner>`. These fields make the active bridge owner visible without adding
a second status frame. `sid=00000000` means no v3 owner has completed `hello`
since boot.

## Compatibility

- Existing `FAB1` and `FAB2` clients keep using the same GATT transport.
- `runtime/capabilities` advertises `session=3` and keeps
  `features=transfer_activity`.
- A v3 client should call `hello` and treat the returned `sid` plus `sid`/`bo` in
  `runtime/status` as the current firmware-side ownership signal.
- Handoff request, heartbeat, release, and explicit cancel semantics are
  reserved for a later firmware slice after companion clients can rely on this
  first session handshake.
