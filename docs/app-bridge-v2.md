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
  `key=value` payload. Runtime v2 keeps backward-compatible keys
  `runtime=1`, `fab=2`, and `session=3`, and advertises `status=2`,
  `packages=1`, `radio=2`, `sd=1`, plus feature flags such as
  `transfer_activity`, `pkg_state`, and `radio_v2`.
- `status` returns `runtime/status` with compact schema v2 fields:
  `schema`, `fw`, `commit`, `dirty`, `origin`, `api`, `target`, `transfer`,
  `sd`, `pkg`, `sid`, `bo`, `radio`, and `owner`. `sd=1` means the SD card is
  mounted and ready; `sd=0` means package state cannot be trusted yet.
  `pkg=1` means `/.tumoflip/package-state.txt` is present on a ready SD card;
  `pkg=0` means package state is not installed or the SD card is unavailable.
  `sid` and `bo` identify the current v3 session and bridge owner.
  `radio` is the compact numeric `SubGhzRadioBrokerState`: `0=idle`,
  `1=acquired`, `2=probing`, `3=initialized`, `4=rx`, `5=tx`, `6=async_rx`,
  `7=async_tx`, `8=cleanup`, `9=external_power_on`, `10=releasing`,
  `11=error`. The command is read-only and designed to fit in one FAB2
  response frame. Long owner names may be shortened in this compact status
  response.
- `hello` implements the first App Bridge v3 session layer documented in
  `docs/app-bridge-v3.md`.
- An unknown command returns `runtime/error`, sets response and error flags,
  and carries `unsupported_command`.
- Invalid chunk order and oversized Runtime payloads return `runtime/error`.

Every response carries the request ID of the command. Runtime responses are
semantic acknowledgements; the acknowledgement-requested flag is reserved for
future generic delivery acknowledgements.

## App Events

FAPs may also emit best-effort events to the paired central. These events use
the same FAB2 frame format but are not Runtime command responses unless the
response flag is set.

### WiFi Mapper live relay

`WiFi Mapper` can relay live ESP32 scan output to the iOS Companion after the
user explicitly arms it on the Flipper.

| Field | Value |
|---|---|
| App ID | `wifi_mapper` |
| Command | `live_line` |
| Request ID | `0` |
| Flags | `0` |
| Chunk index/count | `0/1` |
| Payload | UTF-8 text with one or more raw UART lines separated by `\n` |
| Payload limit | `150` bytes |

The relay is opt-in, session-local, and best-effort. It does not request
acknowledgements and it does not replay missed events. If the phone is
disconnected or App Bridge is disabled, the firmware may drop the event while
continuing to write the local WiFi Mapper CSV log.

## Compatibility

The firmware accepts both `FAB1` and `FAB2`. Existing FAP subscribers receive
the same event fields as before; v2 metadata is appended to `BtAppBridgeEvent`.
An iOS client should probe `runtime/capabilities` with a short timeout and fall
back to `FAB1` when no valid response arrives.
