# ARF Sub-GHz Full

`ARF Sub-GHz Full` is a lightweight external launcher. It provides one entry
point for the external ARF Standard Sub-GHz backend and the isolated ARF tools
without loading all their code into the same process.

The Desktop `Sub-GHz` entry opens `ARF Sub-GHz Full` when
`/ext/apps/ARF Tools/arf_subghz_full.fap` is present on SD, and falls back to
the standard core Sub-GHz app when it is missing. The former Desktop
`Sub-GHz Remote` slot opens the `ARF Tools` folder, where the Full launcher and
ARF Frequency Analyzer are visible. The remaining child FAPs are stored in
`/ext/apps_data/arf_subghz_full/modules`.

## Included workflows

- decoded and RAW receive through `arf_subghz_standard.fap`;
- saved-signal browsing and transmit through `arf_subghz_standard.fap`;
- manual signal generators through `arf_subghz_standard.fap`;
- frequency analyzer through a dedicated visible FAP;
- radio and external CC1101 settings through `arf_subghz_standard.fap`;
- receiver settings and live Protocol Pack switching;
- Protocol Pack Inspector;
- RPC and file-launch handling.

## RAM model

Full queues the selected child in Loader and exits. This gives each utility the
available application heap instead of linking all Sub-GHz scenes and ARF workers
into one FAP. The standard backend remains a large FAP, so it intentionally
excludes ARF-specific Saved-menu actions; PSA, Counter, Car Emulate,
ProtoPirate, RollJam, and Bruteforcer stay in separate child FAPs.

## Validation boundary

The system Sub-GHz application stays in firmware as a fallback. It must not be
removed until `arf_subghz_standard.fap` passes hardware testing for receive,
RAW, saved transmit, generators, radio settings, external CC1101, and Protocol
Packs without heap exhaustion or navigation regressions.
