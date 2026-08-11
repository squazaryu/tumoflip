# Tumoflip protected ESP Flasher

This GPL-3.0 application is maintained in Tumoflip so its on-device contract with
TumoCompanion cannot be silently replaced by a Community Pack binary.

The protected source is based on ESP Flasher 1.9 by 0xchocolate and keeps its
original attribution, license, offline Quick Flash, and Manual Flash workflows.
Tumoflip adds a manifest-first **Flash Package** workflow for packages staged by
TumoCompanion under `/ext/apps_data/esp_flasher`.

ESP Flasher 1.12 keeps the exact C5 package profile at 115200 baud after a
physical 1.14.1 package flash completed programming but timed out during the ROM
MD5 command at turbo speed. C5 verification has a 10-second minimum timeout and
one bounded read-only retry; it never rewrites the segment on a verification
retry. If flash-size detection is unavailable, the exact ESP32-C5-DevKitC-1
profile uses its documented 4 MiB size instead of the library's generic 2 MiB
fallback. Any remaining verification failure still prevents target reset.

Manifest schema v1 deliberately accepts only the two recipes already covered by
the product contract: hardware-accepted `esp32c5devkitc1` compatibility packages
and authoritative Module One `v6_1` factory packages. Other board manifests stay
hidden from package flashing until their role/offset recipe receives an explicit
source review and hardware gate; Manual Flash remains available for expert use.

Upstream: <https://github.com/0xchocolate/flipperzero-esp-flasher>

Community Pack integration source: <https://github.com/xMasterX/all-the-plugins>
