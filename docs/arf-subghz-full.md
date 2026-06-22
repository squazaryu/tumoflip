# ARF Sub-GHz Full

`ARF Sub-GHz Full` is an external FAP built from the same source files as the
system Sub-GHz application. This guarantees parity for normal workflows while
the FAP is evaluated as a future Desktop replacement.

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

The receiver/history, RAW, transmitter, frequency analyzer, and generator
state are allocated on demand in the external build. Returning to the start
menu and opening another top-level tool releases the previous tool state.

Dedicated ARF utilities remain separate FAPs. Full queues the selected child
and then itself in Loader, exits, and is reopened after the child terminates.
This gives each utility the available application heap instead of linking all
ARF scenes and workers into one process.

## Validation boundary

The system Sub-GHz application stays in firmware and remains the Desktop entry
until receive, RAW, saved transmit, generator, analyzer, external CC1101,
Protocol Pack, and child-return workflows have passed hardware testing. Core
Sub-GHz must not be removed solely because the external FAP builds or passes
APPCHK.
