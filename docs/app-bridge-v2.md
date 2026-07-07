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
in order. The firmware Runtime commands are single-frame commands; chunked
`runtime` requests return an explicit error.

## Runtime commands

The system app ID is `runtime`.

- `ping` returns `runtime/pong` with payload `ok`.
- `capabilities` returns `runtime/capabilities` with a semicolon-separated
  `key=value` payload. Runtime v2 keeps backward-compatible keys
  `runtime=1`, `fab=2`, and `session=3`, and advertises `status=2`,
  `trace=1`, `twin=1`, `pkg=1`, `radio=2`, `sd=1`, plus compact feature
  flags in `feat`, currently `pkg`, `radio`, `trace`, and `twin`.
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
- `trace` returns `runtime/trace` with compact schema v1 fields:
  `schema`, `depth`, and `count`, followed by pipe-delimited ring entries.
  Each entry is `code,command,result`, where `code` currently uses `r` for
  received commands, `t` for successful responses, `e` for errors, and `s` for
  session ownership. `command` is the first character of the related command
  token.
  The snapshot is bounded to one FAB2 response frame and is intended for
  Companion diagnostics, not full persistent logging. Example:
  `schema=1;depth=8;count=2|r,s,o|t,s,o`.
- `twin` returns `runtime/twin` with the compact Device Twin schema v1 for the
  current Flipper state. Fields are `fw` firmware version, `cm` commit, `dy`
  dirty flag, `sd` SD readiness, `pkg` package-state presence, `bat` battery
  percentage, `rf` Radio Broker state, `ro` radio owner, `sid` App Bridge
  session ID, and `bo` bridge owner. The payload is read-only and bounded to
  one FAB2 response frame.
- `hello` implements the first App Bridge v3 session layer documented in
  `docs/app-bridge-v3.md`.
- An unknown command returns `runtime/error`, sets response and error flags,
  and carries `badcmd`.
- Chunked Runtime requests return `runtime/error` with `chunk`.

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

### BLE GATT Lab diagnostics

`BLE GATT Lab` is a development FAP for safe App Bridge request/response tests.
It does not scan BLE devices and it does not install a custom GATT profile; it
uses the existing authenticated App Bridge service.

| Field | Value |
|---|---|
| App ID | `ble_gatt_lab` |
| Commands | `ping`, `status`, `echo` |
| Responses | `pong`, `status`, `echo`, `error` |
| Flags | Responses set `0x02`; errors set `0x02 | 0x04` |
| Payload limit | UTF-8 text, clipped to one FAB2 frame |
| Local log | `/ext/apps_data/ble_gatt_lab/ble_gatt_lab_YYYYMMDD_HHMMSS.csv` |

The FAP also emits best-effort `ble_gatt_lab/event` frames when opened and when
the user presses `OK`. These event frames use request IDs generated locally and
do not request acknowledgements.

### App Bridge Terminal

`App Bridge Terminal` is a whitelisted Companion-to-FAP command bus for
interactive App Bridge diagnostics. It is not a shell and does not launch apps
or execute arbitrary code.

| Field | Value |
|---|---|
| App ID | `app_bridge_terminal` |
| Commands | `hello`, `ping`, `status`, `help`, `echo`, `emit`, `release` |
| Responses | `hello`, `pong`, `status`, `help`, `echo`, `emit`, `release`, `error` |
| Flags | Responses set `0x02`; errors set `0x02 | 0x04` |
| Local log | `/ext/apps_data/app_bridge_terminal/terminal_YYYYMMDD_HHMMSS.csv` |

`hello` opens a bounded session with payload `owner=<token>` and returns
`schema=1;sid=<hex>;owner=<token>;timeout_ms=30000;commands=...`. Commands that
change terminal state (`echo`, `emit`, and `release`) require the current
`sid=<hex>` in their payload. `emit` responds with `app_bridge_terminal/emit`
payload `ok` and then emits a best-effort `app_bridge_terminal/event` frame
with payload `schema=1;type=remote;seq=<n>;owner=<token>;text=<text>`. The user
can also press `OK` on the FAP to emit a `type=manual` event.

### Tumo Macro Deck events

`Tumo Macro Deck` can emit best-effort App Bridge events from local SD-backed
macros. The macro runner does not require a Companion connection; a failed send
is recorded in the local run log and then follows the macro policy.

| Field | Value |
|---|---|
| App ID | `tumo_macro_deck` |
| Command | `event` |
| Request ID | Generated locally per event |
| Flags | `0` |
| Payload | User-authored UTF-8 payload from `ble_event ...` |
| Local log | `/ext/apps_data/tumo_macro_deck/runs/run_YYYYMMDD_HHMMSS.csv` |

Signal-emitting macro steps (`ir`, `gpio`) are parsed and require on-device
confirmation before execution. In the first safe increment they intentionally
return `unsupported` after confirmation, so macro policy and logging can be
validated before hardware output is enabled.

## Compatibility

The firmware accepts both `FAB1` and `FAB2`. Existing FAP subscribers receive
the same event fields as before; v2 metadata is appended to `BtAppBridgeEvent`.
An iOS client should probe `runtime/capabilities` with a short timeout and fall
back to `FAB1` when no valid response arrives.
