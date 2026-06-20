# Tumoflip App Bridge v2

App Bridge v2 (`FAB2`) is a backward-compatible protocol carried by the same
authenticated GATT service and characteristics as legacy `FAB1`.

## Frame

All integer fields are little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `FAB2` |
| 4 | 1 | Flags |
| 5 | 1 | UTF-8 app ID length, 1...32 |
| 6 | 1 | UTF-8 command length, 1...32 |
| 7 | 1 | Zero-based chunk index |
| 8 | 1 | Chunk count, 1...255 |
| 9 | 1 | Reserved, write zero |
| 10 | 2 | Payload length, 0...160 |
| 12 | 4 | Request ID |
| 16 | variable | App ID, command, payload |

Flags:

- `0x01`: acknowledgement requested.
- `0x02`: response.
- `0x04`: error response.

The complete frame must not exceed 244 bytes. Chunks belonging to one request
must have the same request ID, app ID, command, and chunk count, and must arrive
in order. The firmware Runtime reassembles up to 512 bytes for its own commands.

## Runtime commands

The system app ID is `runtime`.

- `ping` returns `runtime/pong` with payload `ok`.
- `capabilities` returns `runtime/capabilities` with a semicolon-separated
  `key=value` payload.
- `radio_status` reports whether the Sub-GHz Broker is busy, its owner,
  selected device, and external-power state. It does not acquire the radio.
- An unknown command returns `runtime/error`, sets response and error flags,
  and carries `unsupported_command`.
- Invalid chunk order and oversized Runtime payloads return `runtime/error`.

Every response carries the request ID of the command. Runtime responses are
semantic acknowledgements; the acknowledgement-requested flag is reserved for
future generic delivery acknowledgements.

## Compatibility

The firmware accepts both `FAB1` and `FAB2`. Existing FAP subscribers receive
the same event fields as before; v2 metadata is appended to `BtAppBridgeEvent`.
An iOS client should probe `runtime/capabilities` with a short timeout and fall
back to `FAB1` when no valid response arrives.
