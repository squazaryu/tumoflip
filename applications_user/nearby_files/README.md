# Nearby Files (Tumoflip)

Nearby Files scans Sub-GHz, NFC, RFID, and iButton captures on the SD card, keeps
only files with `Lat:`/`Lon:` metadata, and sorts them by distance from the current
position. Selecting a row opens the file in its owning Flipper application. The menu
can also write the current position into a selected file.

The default GPS source is a one-shot location request to TumoCompanion over the
authenticated App Bridge (`gps_once`). A separate UART module is still available from
**GPS Source → UART module (NMEA)** for standalone operation. The phone source is
deliberately one-shot: it does not add the upstream GPS/Network RPC services or a
continuous location stream to the firmware.

This app is adapted from
https://github.com/Stichoza/flipper-nearby-files at the Community Pack source snapshot
used for the adaptation. The original application and its MIT license are retained in
this directory. The `minmea` private library keeps its upstream license files.

The FAP is package-only and is delivered through Tumoflip FW Packages at
`/ext/apps/GPIO/nearby_files.fap`; the upstream Community Apps copy is intentionally
excluded from the TumoCompanion install list so only the API-88.6 adaptation is used.
