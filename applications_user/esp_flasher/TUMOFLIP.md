# Tumoflip protected ESP Flasher

This GPL-3.0 application is maintained in Tumoflip so its on-device contract with
TumoCompanion cannot be silently replaced by a Community Pack binary.

The protected source is based on ESP Flasher 1.9 by 0xchocolate and keeps its
original attribution, license, offline Quick Flash, and Manual Flash workflows.
Tumoflip adds a manifest-first **Flash Package** workflow for packages staged by
TumoCompanion under `/ext/apps_data/esp_flasher`.

ESP Flasher 1.13 keeps every accepted Flash Package profile at 115200 baud after
physical C5 acceptance and a Module One 1.14.1 failure showed programming can
complete before the following ROM MD5 command times out at turbo speed. ESP32 and
ESP32-C5 verification have a 10-second minimum timeout and one bounded read-only
retry; a verification retry never erases or rewrites the segment. Manual and
Quick Flash retain their explicit turbo workflows. If flash-size detection is
unavailable, the exact ESP32-C5-DevKitC-1 profile uses its documented 4 MiB size
instead of the library's generic 2 MiB fallback. Any remaining verification
failure still prevents target reset.

Manifest schema v1 deliberately accepts only the two recipes already covered by
the product contract: hardware-accepted `esp32c5devkitc1` compatibility packages
and authoritative Module One `v6_1` factory packages. Other board manifests stay
hidden from package flashing until their role/offset recipe receives an explicit
source review and hardware gate; Manual Flash remains available for expert use.

Upstream: <https://github.com/0xchocolate/flipperzero-esp-flasher>

Community Pack integration source: <https://github.com/xMasterX/all-the-plugins>
