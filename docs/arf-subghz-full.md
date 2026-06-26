# ARF Sub-GHz Full

`ARF Sub-GHz Full` is a lightweight external launcher. It provides one entry
point for the stable core Sub-GHz app and isolated ARF tools without loading all
their code into the same process.

The Desktop `Sub-GHz` entry opens the standard core Sub-GHz app. The former
Desktop `Sub-GHz Remote` slot opens the `ARF Tools` folder, where the Full
launcher, ARF Frequency Analyzer, Sub-GHz RAW Edit, and KeeLoq Keystore
Decryptor are visible. BLE Killer is also visible as an opt-in Module One
ESP32 UART controller, and Garage Door Remote is visible as a separate
ProtoPirate-derived garage/gate tool. RollJam Standalone is visible as a
separate opt-in RollJam 2.0 test app. The remaining child FAPs are stored in
`/ext/apps_data/arf_subghz_full/modules`.

## Included workflows

- decoded and RAW receive through the core Sub-GHz app;
- saved-signal browsing and transmit through the core Sub-GHz app;
- manual signal generators through the core Sub-GHz app;
- frequency analyzer through a dedicated visible FAP;
- RAW `.sub` trimming and cleanup through Sub-GHz RAW Edit;
- KeeLoq keystore decrypt/export through KeeLoq Keystore Decryptor;
- external Module One ESP32 BLE UART control through BLE Killer;
- garage/gate capture and emulate workflows through Garage Door Remote;
- standalone RollJam 2.0 testing through RollJam Standalone;
- radio and external CC1101 settings through the core Sub-GHz app;
- receiver settings and live Protocol Pack switching;
- Protocol Pack Inspector;
- RPC and file-launch handling.

## RAM model

Full queues the selected child in Loader and exits. This gives each utility the
available application heap instead of linking all Sub-GHz scenes and ARF workers
into one FAP. A full standard Sub-GHz external backend exceeded available heap
on hardware, so standard workflows stay in the core app. PSA, Counter, Car
Emulate, ProtoPirate, the original RollJam child module, and Bruteforcer stay in
separate child FAPs. RollJam Standalone is a visible opt-in test app and does
not replace the original child module yet.

## Shared Sub-GHz helpers

ARF child apps may reuse pure helpers from `lib/subghz` when they do not import
firmware-only state or radio lifecycle assumptions. The combined hopper uses the
shared `subghz_hopper_plan_next()` helper only to calculate the next
frequency/preset indexes; ARF still owns its RSSI timeout, preset reload, and
child-FAP profile behavior.

## Validation boundary

The system Sub-GHz application stays in firmware as the primary receiver and
transmitter surface. It should not be replaced by an external FAP unless the
Sub-GHz app is first split into a smaller service/client architecture that can
be validated without heap exhaustion or navigation regressions. The repository
tracks this as a deliberate architecture boundary in
[Sub-GHz Architecture](subghz-architecture.md).
