# WiFi Mapper

`WiFi Mapper` is a Module One ESP32 Wi-Fi external app. It is intentionally a
small passive UART logger: the Flipper sends ESP32 Marauder-compatible scan
commands, records UART output, and stores each received printable line as CSV
on SD. `Scan All` is the default mode because it is supported by the current
Module One ESP32 Wi-Fi app set; `Scan AP` remains available for Marauder builds
that expose `scanap`. `Wardrive` sends `wardrive -serial` and records GPS fields
when the ESP32/Marauder output includes them.

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

- `OK`: start or stop logging with the selected scan mode.
- Hold `OK`: open the latest session summary.
- `Left` / `Right`: switch between `Scan All`, `Scan AP`, and `Wardrive`.
- `Up`: send the selected scan command.
- `Down`: send `stopscan`.
- `Back`: exit.

## Session Summary

Hold `OK` on the live screen to open `Last Session`. The app reads the newest
`wifi_*.csv` file from the sessions directory and shows:

- total parsed AP rows;
- unique BSSID count, capped in-app to the first 64 unique BSSIDs;
- GPS-tagged row count;
- mapped unique BSSID count for clean exports;
- duplicate GPS sample count;
- best and average RSSI;
- busiest detected channel.

On the session screen:

- `OK`: export the newest session as GeoJSON.
- `Right`: switch export mode between `Clean` and `Raw`.
- `Up`: refresh the summary.
- `Back`: return to the live logger.

`Clean` export groups GPS rows by BSSID and writes one point per detected
network using the best RSSI sample as the map location. It includes `samples`,
`best_rssi`, `last_rssi`, `avg_rssi`, `first_tick_ms`, and `last_tick_ms`.
`Raw` export writes every GPS-tagged AP row as a separate feature. Exported
features include only AP rows that have valid latitude and longitude, so normal
`Scan All` sessions without GPS data can produce `No GPS rows`.

## Companion Map

The iOS companion can read GeoJSON exports from:

```text
/ext/apps_data/wifi_mapper/exports
```

Open the `WiFi` tab and use `WiFi Mapper` to view clean or raw exports on a map.
The companion parser keeps the Flipper export format simple: coordinates come
from GeoJSON `Point` geometry, while `ssid`, `bssid`, `auth`, `channel`, RSSI,
`samples`, and tick metadata stay in feature properties.

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
to be treated as the canonical tumoflip mapper, so tumoflip keeps `WiFi Mapper`
as its own small, auditable logger.
