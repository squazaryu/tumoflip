# WiFi Mapper

`WiFi Mapper` is a Module One ESP32 Wi-Fi external app. It is intentionally a
small passive UART logger: the Flipper sends ESP32 Marauder-compatible scan
commands, records UART output, and stores each received printable line as CSV
on SD. `Scan All` is the default mode because it is supported by the current
Module One ESP32 Wi-Fi app set; `Scan AP` remains available for Marauder builds
that expose `scanap`.

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

## Controls

- `OK`: start or stop logging with the selected scan mode.
- `Left` / `Right`: switch between `Scan All` and `Scan AP`.
- `Up`: send the selected scan command.
- `Down`: send `stopscan`.
- `Back`: exit.

## Log Format

The current log format is deliberately raw:

```csv
tick_ms,raw
12345,"example uart line"
```

Lines starting with `WIFI,` are counted separately by the app. A future ESP32
firmware profile can emit structured `WIFI,...` records with BSSID, SSID,
RSSI, channel, security, and optional GPS fields; the Flipper-side logger can
then render those into a map or hand them to the iOS companion without changing
the basic SD storage layout.
