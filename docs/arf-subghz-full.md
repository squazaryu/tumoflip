# ARF Sub-GHz Full

`ARF Sub-GHz Full` is a lightweight external launcher. It provides one entry
point for the system Sub-GHz application and the isolated ARF tools without
loading all their code into the same process.

The Desktop `Sub-GHz` entry opens the standard core Sub-GHz app. The former
Desktop `Sub-GHz Remote` slot opens the `ARF Tools` folder, where only the Full
launcher is visible. Child FAPs are stored in
`/ext/apps_data/arf_subghz_full/modules`.

## Included workflows

- decoded and RAW receive;
- saved-signal browsing and transmit;
- manual signal generators;
- frequency analyzer;
- radio and external CC1101 settings;
- receiver settings and live Protocol Pack switching;
- Protocol Pack Inspector;
- RPC and file-launch handling.

## RAM model

Full queues the selected child in Loader and exits. This gives each utility the
available application heap instead of linking all Sub-GHz scenes and ARF workers
into one FAP. The single-FAP parity build reached 159 KB and left only about
2 KB of free heap on hardware, so it was rejected after the first device test.

## Validation boundary

The system Sub-GHz application stays in firmware and remains the implementation
for normal receive, RAW, saved transmit, generators, radio settings, external
CC1101, RPC, and Protocol Packs. It must not be removed until these workflows
have been extracted into smaller external modules and passed hardware testing.
