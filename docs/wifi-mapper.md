# WiFi Mapper

`WiFi Mapper` is a Module One ESP32 Wi-Fi external app. It is intentionally a
small passive UART logger: the Flipper sends the ESP32 Marauder-compatible
`scanap` command, records UART output, and stores each received printable line
as CSV on SD.

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

- `OK`: start or stop logging.
- `Up`: send `scanap`.
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
