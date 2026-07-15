# TumoSurvey Network Explorer

`TumoSurvey` is the product form of the existing `wifi_mapper` Module One app.
It remains a passive UART scanner, but adds bounded live channel/security
insights, atomic sessions, saved-session navigation, pinned site baselines,
exact bounded AP change lists, an RSSI Locator, GeoJSON export, and an automatic
Companion live relay while a survey is recording. The app ID and
`/ext/apps_data/wifi_mapper` paths remain unchanged for compatibility.

It does not send deauth, beacon spam, PMKID sniffing, evil portal, or packet
capture commands. Those stay out of this app so the first mapping layer is safe
to test and small enough to keep as a normal FAP.

## Location

Release packages install it under:

```text
/ext/apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap
```

Session logs are written to:

```text
/ext/apps_data/wifi_mapper/sessions/wifi_YYYYMMDD_HHMMSS.csv
```

GeoJSON exports are written to:

```text
/ext/apps_data/wifi_mapper/exports/wifi_YYYYMMDD_HHMMSS_clean.geojson
/ext/apps_data/wifi_mapper/exports/wifi_YYYYMMDD_HHMMSS_raw.geojson
```

## Controls

- `OK`: start or stop a survey with the selected mode.
- `Left` or `Up`: change mode while idle.
- `Right`: open live channel/security insights. From there, `Down` opens About.
- Hold `OK`: open saved sessions.
- Hold `Down`: manually override the automatic Companion live relay.
- `Back`: exit.

## Session Summary

Hold `OK` on the live screen to open saved sessions. The app reads compatible
`wifi_*.csv` files and shows four pages:

- total parsed AP rows;
- unique BSSID count, capped to a bounded in-memory set with a visible `+` when
  additional networks were observed;
- GPS-tagged row count;
- mapped unique BSSID count for clean exports;
- duplicate GPS sample count;
- best and average RSSI;
- Open/Legacy/WPA2/WPA3 counts;
- per-channel counts and the busiest detected channel;
- bounded deltas against the immediately preceding session. If either session
  exceeds the on-device AP limit, the screen directs full comparison to the
  Companion instead of presenting misleading partial deltas.

On the session screen:

- `Left` / `Right`: previous or next session.
- `Up` / `Down`: change Summary/Security/Channels/Baseline page.
- `OK`: export the selected session as GeoJSON.
- `OK` on the Baseline page: open Survey Inspector instead of exporting.
- Hold `OK`: switch export mode between `Clean` and `Raw`.
- `Back`: return to the live logger.

The active CSV is first written as `*.csv.part`, synced, and atomically renamed
to `*.csv` on Stop. Interrupted partial sessions are not shown as completed.

`Clean` export groups GPS rows by BSSID and writes one point per detected
network using the best RSSI sample as the map location. It includes `samples`,
`best_rssi`, `last_rssi`, `avg_rssi`, `first_tick_ms`, and `last_tick_ms`.
`Raw` export writes every GPS-tagged AP row as a separate feature. Exported
features include only AP rows that have valid latitude and longitude, so normal
`Scan All` sessions without GPS data can produce `No GPS rows`.
Marauder/ESP32 rows with `0,0` coordinates are treated as "GPS has no fix" and
are not exported as map points.

## Survey Inspector

Open a completed session, move to its Baseline page, and press `Inspect`.
Inspector keeps the selected session as `Now` and compares it with a pinned
baseline stored at:

```text
/ext/apps_data/wifi_mapper/baseline.txt
```

The reference contains only a validated `wifi_*.csv` file name. The `Pin`
button stores the selected completed session and survives an app restart.
Inspector then
reports exact bounded entries by BSSID:

- `New`: present now but absent from the baseline;
- `Gone`: present in the baseline but absent now;
- `Changed`: the same BSSID changed SSID, security text, or channel;
- `Current`: every AP in the selected current session.

`Changes` opens the first non-empty change category and `All` opens `Current`.
In the result browser, `Left`/`Right` changes the item and `Up`/`Down` changes
the category. `Locate` is available for current entries, but not for `Gone`.

Locator uses the supported passive `scanall` UART command and filters parsed
observations to the selected BSSID. It shows current and peak RSSI plus a bounded
`Warmer`/`Colder`/`Steady` trend. `Stop`, `Back`, and app exit send `stopscan`
before returning to the normal survey flow. If either snapshot exceeds the
32-AP Inspector limit, the UI marks the comparison as partial rather than
claiming the result is exhaustive.

Current Marauder builds no longer expose the older AP-only `scanap` command,
so the main survey selector contains only `Scan All` and `Wardrive`.

## Companion Map

The iOS companion can read GeoJSON exports from:

```text
/ext/apps_data/wifi_mapper/exports
```

Open `TumoSurvey` from the WiFi tile to view live networks, the iPhone-GPS map,
or clean/raw exports.
The companion parser keeps the Flipper export format simple: coordinates come
from GeoJSON `Point` geometry, while `ssid`, `bssid`, `auth`, `channel`, RSSI,
`samples`, and tick metadata stay in feature properties.

## Companion Live Relay

Starting a survey automatically arms live BLE relay; Stop flushes pending data
and disables it. The relay is not persisted and is never started in the
background. Hold `Down` remains a session-local manual override, and the
on-screen `BLE` marker shows the current state.

While armed, printable UART lines from the ESP32 are relayed over App Bridge v2
as best-effort events:

- app ID: `wifi_mapper`
- command: `live_line`
- request ID: `0`
- flags: `0`
- chunk index/count: `0/1`
- payload: UTF-8 text containing one or more raw UART lines separated by `\n`
- payload limit: `150` bytes per event, below the FAB2 `160` byte cap

The companion also receives `wifi_mapper/survey_start` when a local session
begins and `wifi_mapper/survey_stop` after buffered lines are flushed and the
session is committed. Both events carry a compact
`schema=1;mode=...;file=...;aps=...;obs=...` payload, so live dashboards can
reset and close a session without guessing from BLE timing gaps.

Long individual lines are clipped to the relay payload limit. Multiple short
lines may be coalesced into one event to reduce pressure on the shared BLE/RPC
link. Pending lines are flushed when the relay is disabled, logging stops, or
the app exits. If App Bridge is disabled, the phone is disconnected, or BLE
cannot accept the event, that flush is dropped; the CSV session log remains the
source of truth for offline review.

## Log Format

The log keeps the raw UART line and also extracts AP rows from the current
Marauder `scanall` and `wardrive -serial` formats. New sessions use:

```csv
tick_ms,type,rssi,channel,bssid,ssid,auth,lat,lon,alt,accuracy,raw
12345,ap,-59,3,"00:11:22:33:44:55","example","","","","","","-59 Ch: 3 00:11:22:33:44:55 ESSID: example"
```

Older sessions with the previous shorter form still load in the summary screen:

```csv
tick_ms,type,rssi,channel,bssid,ssid,raw
```

For `Scan All`, the geo fields are empty. For `Wardrive`, rows parsed from
Marauder `wardrive -serial` output use `type=wardrive` and include auth,
latitude, longitude, altitude, and accuracy when present. The parser tolerates
minor Marauder formatting differences such as `Ch`/`ch`, `ESSID`/`SSID`, and
wardrive rows without the final `WIFI` field when the coordinate fields are
present.

The on-screen `WiFi` counter increments for Marauder AP lines and for future
lines starting with `WIFI,`. A structured ESP32 firmware profile can emit:

```csv
WIFI,00:11:22:33:44:55,example,WPA2,-59,3,55.751244,37.618423,120,5
```

Those records are stored with `type=wifi` and can feed the same summary and
GeoJSON export path.

## Existing WiFi Mapping FAP

Some SD app bundles include a separate `WiFi Mapping` FAP. The current bundled
binary appears to be a raw USB/UART logger based on an echo worker and writes to
`/ext/apps_data/wifi_map/wifi_map_data.csv`. It does not expose enough metadata
to be treated as the canonical tumoflip mapper, so TumoSurvey remains the
auditable product while retaining the compatible `wifi_mapper` app ID.
