# Tumoflip protected ESP Flasher

This GPL-3.0 application is maintained in Tumoflip so its on-device contract with
TumoCompanion cannot be silently replaced by a Community Pack binary.

The protected source is based on ESP Flasher 1.9 by 0xchocolate and keeps its
original attribution, license, offline Quick Flash, and Manual Flash workflows.
Tumoflip adds a manifest-first **Flash Package** workflow for packages staged by
TumoCompanion under `/ext/apps_data/esp_flasher`.

Manifest schema v1 deliberately accepts only the two recipes already covered by
the product contract: hardware-accepted `esp32c5devkitc1` compatibility packages
and authoritative Module One `v6_1` factory packages. Other board manifests stay
hidden from package flashing until their role/offset recipe receives an explicit
source review and hardware gate; Manual Flash remains available for expert use.

Upstream: <https://github.com/0xchocolate/flipperzero-esp-flasher>

Community Pack integration source: <https://github.com/xMasterX/all-the-plugins>
