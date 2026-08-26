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
  `trace=1`, `twin=1`, `pkg=1`, `radio=2`, `sd=1`, and `diag=1`. The compact
  aliases `rc=1`, `rs=2`, and `rp=1` advertise the read-only `radio_caps`,
  `radio_sessions` plus `radio_sessions_export`, and `radio_protocols`
  commands respectively. The legacy `time=1`, `gps=1`, and `net=1` keys are
  retained for clients that probe those services directly. The `feat` value
  retains the core feature tokens (`pkg`, `radio`, `trace`, `twin`, `transfer`,
  and `fabric`) while the complete
  capability payload remains within one FAB2 frame.
- `status` returns `runtime/status` with compact schema v2 fields:
  `schema`, `fw`, `commit`, `dirty`, `origin`, `api`, `target`, `transfer`,
  `sd`, `pkg`, `sid`, `bo`, `radio`, and `owner`. `sd=1` means the SD card is
  mounted and ready; `sd=0` means package state cannot be trusted yet.
  `transfer=1` means the companion has marked an active BLE file transfer;
  `transfer=0` means no transfer activity is currently advertised.
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
  `schema`, `depth`, `count`, and `drop`, followed by pipe-delimited ring entries.
  Each entry is `code,command,result`, where `code` currently uses `r` for
  received commands, `t` for successful responses, `e` for errors, and `s` for
  session ownership. `command` is the first character of the related command
  token.
  `drop` increments when the bounded ring overwrites older entries.
  The snapshot is bounded to one FAB2 response frame and is intended for
  Companion diagnostics, not full persistent logging. Example:
  `schema=1;depth=8;count=2;drop=0|r,s,o|t,s,o`.
- `twin` returns `runtime/twin` with the compact Device Twin schema v1 for the
  current Flipper state. Fields are `fw` firmware version, `cm` commit, `dy`
  dirty flag, `sd` SD readiness, `pkg` package-state presence, `bat` battery
  percentage, `chg` charging state, `otg` OTG power state, `heap` maximum free
  heap block, `rf` Radio Broker state, `ro` radio owner, `sid` App Bridge
  session ID, and `bo` bridge owner. The payload is read-only and bounded to
  one FAB2 response frame.
- `diagnostics` runs bounded, non-destructive checks and returns one status per
  check: `identity`, `heap`, `storage`, `battery`, `radio`, `ble`, `display`,
  `nfc`, `gpio`, and `input`. Display, NFC, GPIO, and input are explicitly marked `skip`
  because they require an interactive or external fixture; this is not a
  failure.
- `radio_caps` returns the supported internal, external, and dual-radio
  profiles with bounded RX/TX envelopes and the external-power requirement.
- `radio_sessions` returns the last eight bounded Standard, ARF, and analyzer
  sessions without payload data.
- `radio_sessions_export` atomically replaces the deterministic CSV snapshot
  at `/.tumoflip/radio-sessions.csv`; it is also available from the USB CLI as
  `tumoflip radio_sessions_export`.
- `radio_protocols` returns a bounded registry for protocol families shared by
  Standard Sub-GHz and ARF. Each entry reports receive/transmit support,
  modulation, and the permitted receive range; unknown protocol names are
  rejected rather than treated as implicitly safe.
- `journal` returns the last persisted crash/watchdog/reset classification;
  the journal never contains application payloads or user data.
- `diagnostics_export` runs the same checks as `diagnostics` and persists the
  redacted report at `/.tumoflip/diagnostics-last.txt` using an atomic replace.
- `transfer_begin`, `transfer_progress`, and `transfer_end` return matching
  `runtime/*` `ok` replies and drive the on-device BLE transfer activity
  indicator. Repeated progress pulses keep the indicator alive; `transfer_end`
  clears it.
- `hello` implements the first App Bridge v3 session layer documented in
  `docs/app-bridge-v3.md`.
- `fabric_caps` returns the bounded TumoFabric v1 reference-package contract
  plus the live `active` and `owner` discovery fields.
  `fabric_open`, `fabric_state`, `fabric_step`, and `fabric_cancel` implement
  the fixed `counter` package described below.
- The legacy `time=1`, `gps=1`, and `net=1` capability keys advertise the
  opt-in `device_services` Companion contract described below; they are kept
  outside the compact `feat` list so the whole payload fits one FAB2 frame.
- An unknown command returns `runtime/error`, sets response and error flags,
  and carries `badcmd`.
- Chunked Runtime requests return `runtime/error` with `chunk`.

Every response carries the request ID of the command. Runtime responses are
semantic acknowledgements; the acknowledgement-requested flag is reserved for
future generic delivery acknowledgements.

### TumoFabric Counter v1

TumoFabric v1 proves deterministic work split between Flipper and Companion
without introducing a general command interpreter. The Flipper owns one
RAM-backed signed counter in the range `-999...999`; the only remote operations
are `inc` and `dec`.

- `fabric_open`: `owner=iphone;pkg=counter;token=<8 hex>`. A new token opens a
  session. Repeating the same owner and token resumes its existing session and
  value. A different owner or token receives `busy`.
- `fabric_state`: `sid=<8 hex>;token=<8 hex>` returns the current value.
- `fabric_step`: `sid=<8 hex>;token=<8 hex>;seq=<decimal>;op=inc|dec`. The next
  sequence is applied once. Repeating the current sequence returns the same
  state with `dup=1`; gaps and stale sequences return `seq`.
- `fabric_cancel`: `sid=<8 hex>;token=<8 hex>` releases and clears the session.

Successful state responses use
`schema=1;pkg=counter;sid=...;token=...;seq=...;value=...;dup=...;persist=ram`.
The token supplements the authenticated BLE link but is not a hardware-backed
node identity. State survives BLE reconnect while the Flipper remains powered;
reboot and explicit cancel clear it. `TumoFabric Node` provides local offline
start, increment, decrement, state inspection, and cancellation. While the
Companion TumoFabric screen is open, it may poll `fabric_caps` and automatically
adopt a session only when `active=1;owner=flipper`; an idle probe must not create
a remote session.

### TumoFabric USB operator plane

The native Mac node uses the physical USB CLI instead of BLE, so it can remain
connected while iPhone owns the App Bridge session. The command is intentionally
bounded and does not provide a general terminal:

- `tumofabric caps`
- `tumofabric state`
- `tumofabric start`
- `tumofabric step inc|dec`
- `tumofabric cancel`
- `tumofabric trace`

Every response is one machine-readable line beginning with `FABRIC schema=1`.
State and capability replies may include `active`, `owner`, `seq`, and `value`,
but the USB surface must not expose the BLE token or session ID. USB `step`
uses the local runtime API and therefore does not replace the current BLE owner
or consume the remote replay sequence. Invalid verbs or arguments return a
bounded error without changing runtime state. Physical USB presence is the
operator trust boundary for start, step, and cancel.

## App Events

FAPs may also emit best-effort events to the paired central. These events use
the same FAB2 frame format but are not Runtime command responses unless the
response flag is set.

### TumoSurvey live relay

`TumoSurvey` relays live ESP32 scan output to the iOS Companion while a survey
is active. Starting a survey is the explicit user action that arms the relay;
Stop flushes pending data and disables it.

| Field | Value |
|---|---|
| App ID | `wifi_mapper` |
| Command | `live_line` |
| Request ID | `0` |
| Flags | `0` |
| Chunk index/count | `0/1` |
| Payload | UTF-8 text with one or more raw UART lines separated by `\n` |
| Payload limit | `150` bytes |

Session boundaries use the same App ID with `survey_start` and `survey_stop`
commands. Their payload is
`schema=1;mode=<n>;file=<csv>;aps=<n>;obs=<n>`; the start event begins with zero
counts and the stop event reports the final bounded survey counters.

The relay is active-session-only and best-effort. It does not request
acknowledgements and it does not replay missed events. If the phone is
disconnected or App Bridge is disabled, the firmware may drop the event while
continuing to write the local TumoSurvey CSV log.

At survey start, TumoSurvey also sends one correlated
`device_services/gps_once` request with `schema=1;purpose=survey`. A valid
iPhone fix is written as a `location` CSV row and used only as a fallback for
subsequent Wi-Fi rows that do not already carry Module One coordinates. A
missing, denied, or timed-out fix changes the on-device GPS chip to `GPS--` but
does not stop UART capture or local logging.

### iPhone device services

`Flipper Companion` and TumoSurvey can request opt-in iPhone field services.
Requests use app ID
`device_services`, FAB2 request IDs, and the acknowledgement-requested flag.
TumoCompanion replies with the same request ID and the response flag.

| Command | Request payload | Successful response |
|---|---|---|
| `time_once` | `schema=1;purpose=<clock|totp>` | `schema=1;unix=...;offset=...` |
| `gps_once` | `schema=1;purpose=<manual|survey|sidecar>` | `schema=1;lat=...;lon=...;alt=...;acc=...;ts=...` |
| `weather_now` | `schema=1` | `schema=1;temp=...;feels=...;wind=...;code=...;at=...` |
| `place_once` | `schema=1` | `schema=1;place=...;region=...;country=...` |
| `release_latest` | `schema=1` | `schema=1;tag=...;name=...;at=...` |
| `journal_append` | `schema=1;kind=...;note=...` | `schema=1;stored=1;sent=<0|1>;delivery=...;id=...` |
| `https_get` | Allowlisted HTTPS URL, one FAB2 frame | `status=<code>;truncated=<0|1>\n<body>` |

Errors set both response and error flags and carry a stable token such as
`disabled`, `permission`, `busy`, `invalid_url`, `forbidden_host`, `timeout`, or
`network`.

Location, network sharing, last-known storage, journal storage, and webhook
delivery are separate switches and are disabled by default in TumoCompanion.
One request runs at a time and receives a bounded background execution window;
background GPS requires Always Location and stops after the one-shot reply.
`time_once` uses the authenticated iPhone session and the iPhone system clock,
but does not require the location or network switches.

Weather, reverse geocoding, and stable-release lookup are named services with
fixed hosts, paths, fields, timeouts, redirect policy, and response limits in
TumoCompanion. Flipper cannot provide their URL or credentials. The legacy
`https_get` diagnostic remains restricted to the exact public GitHub test
endpoint. An optional journal webhook is configured only on iPhone, is HTTPS
only, rejects local/private literal hosts and redirects, and keeps its bearer
token in Keychain.
The contract does not expose raw sockets, WebSocket, arbitrary request headers,
or Flipper-controlled service credentials.

`Tag saved file` asks the user to choose an existing `.sub`, `.nfc`, or `.rfid`
file, obtains one GPS fix, and atomically writes
`<source>.tumoflip.json`. The capture itself is never modified; a previous
sidecar is backed up, the new file is read back byte-for-byte, and any failure
rolls the operation back.

The same transaction is used automatically after successful saves in core
Sub-GHz, NFC, and LF RFID. TumoSurvey writes a sidecar for its committed CSV
session, Field Logger embeds the one-shot iPhone fix in CSV/JSONL/GPX records,
and TumoSpectrum writes sidecars beside saved signal reports. Location failure
never changes or blocks the primary capture.

In `Settings -> Clock`, holding `OK` on the Time row requests `time_once` and
sets the Flipper RTC from the returned local time. Authenticator/TOTP performs
a non-mutating check against the same phone time and shows `TIME!` only when
the RTC or configured time-zone offset is inconsistent.

### BLE GATT Lab diagnostics

`BLE GATT Lab` is a development FAP for safe App Bridge request/response tests
and opt-in BLE client diagnostics.  Passive scanning and GATT client actions
are available only with the full STM32WB stack; on the normal light stack the
commands fail closed and the existing App Bridge remains unchanged.  The FAP
never sends raw HCI commands and all callbacks are bounded.

| Field | Value |
|---|---|
| App ID | `ble_gatt_lab` |
| Commands | `ping`, `status`, `echo`, `scan_start`, `scan_stop`, `connect`, `disconnect`, `services`, `characteristics`, `read`, `write`, `notify` |
| Responses | `pong`, `status`, `echo`, `scan_start`, `scan_stop`, `connect`, `disconnect`, `services`, `characteristics`, `read`, `write`, `notify`, `scan_result`, `gatt_event`, `error` |
| Flags | Responses set `0x02`; errors set `0x02 | 0x04` |
| Payload limit | UTF-8 text, clipped to one FAB2 frame |
| Local log | `/ext/apps_data/ble_gatt_lab/ble_gatt_lab_YYYYMMDD_HHMMSS.csv` |

Binary command payloads are intentionally small and explicit: `scan_start` is
an optional little-endian `uint32_t` duration (the default is 5 seconds and the
HAL clamps it to 1...30 seconds); `connect` is one address-type byte followed
by six address bytes; `characteristics` is two little-endian `uint16_t`
handles; `read` is one little-endian handle; and `write` is a little-endian
handle, a confirmation byte set to `1`, then 1...244 value bytes.  Writes are
always acknowledged by the peer; unconfirmed or oversized writes are rejected.
After a valid write or notification request the Flipper shows an on-device
confirmation screen; only `OK` sends the operation to the peer and `Back`
cancels it.  The request remains pending until one of those keys is pressed.
`notify` is a little-endian CCCD handle, an enabled byte (`0` or `1`), and a
confirmation byte set to `1`. Notification changes require an authenticated
App Bridge request and explicit user confirmation; the FAP never subscribes to
a descriptor implicitly.
Scan results include a bounded address, RSSI, optional local name, and raw
advertising bytes in the authenticated App Bridge response.  The local CSV
log stores only counts, RSSI, lengths, and GATT metadata; peer addresses,
advertising names, write values, and read/notification values are redacted.
GATT events expose bounded handles, UUIDs, a short value preview, and status
codes rather than raw HCI frames.

The FAP also emits best-effort `ble_gatt_lab/event` frames when opened and when
the user presses `OK`. These event frames use request IDs generated locally and
do not request acknowledgements.

### BLE Scanner

`BLE Scanner` is a separate Module One FAP for an explicitly authorized,
receive-only advertising scan. It keeps at most 16 deduplicated peers on the
device, limits one scan to eight seconds, and exposes no GATT or raw-HCI
operation. The FAP reports `UNAVAILABLE` on the light BLE stack; it does not
silently fall back to a peripheral-mode operation. Use `BLE GATT Lab` when a
selected peer must be connected to or inspected.

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
