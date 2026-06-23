# ARF Sub-GHz Full

`ARF Sub-GHz Full` is a lightweight external launcher. It provides one entry
point for the stable core Sub-GHz app and isolated ARF tools without loading all
their code into the same process.

The Desktop `Sub-GHz` entry opens the standard core Sub-GHz app. The former
Desktop `Sub-GHz Remote` slot opens the `ARF Tools` folder, where the Full
launcher and ARF Frequency Analyzer are visible. The remaining child FAPs are stored in
`/ext/apps_data/arf_subghz_full/modules`.

## Included workflows

- decoded and RAW receive through the core Sub-GHz app;
- saved-signal browsing and transmit through the core Sub-GHz app;
- manual signal generators through the core Sub-GHz app;
- frequency analyzer through a dedicated visible FAP;
- radio and external CC1101 settings through the core Sub-GHz app;
- receiver settings and live Protocol Pack switching;
- Protocol Pack Inspector;
- RPC and file-launch handling.

## RAM model

Full queues the selected child in Loader and exits. This gives each utility the
available application heap instead of linking all Sub-GHz scenes and ARF workers
into one FAP. A full standard Sub-GHz external backend exceeded available heap
on hardware, so standard workflows stay in the core app. PSA, Counter, Car
Emulate, ProtoPirate, RollJam, and Bruteforcer stay in separate child FAPs.

## Validation boundary

The system Sub-GHz application stays in firmware as the primary receiver and
transmitter surface. It should not be replaced by an external FAP unless the
Sub-GHz app is first split into a smaller service/client architecture that can
be validated without heap exhaustion or navigation regressions.
