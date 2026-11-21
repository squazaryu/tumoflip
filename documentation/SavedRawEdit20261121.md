# Open a saved RAW capture in RAW Edit

This is the selective Tumoflip counterpart of Lechnio's
[Unleashed #1221](https://github.com/DarkFlippers/unleashed-firmware/pull/1221)
(`6c4574bcb2674c4b6e5630f80ee1202eb4101241`). Keep the existing GPL-licensed
RAW Edit adaptation, author credit and installed ARF Tools location. Do not
import the upstream app wholesale, move data or change Community ownership.

**Sub-GHz → Saved → open a RAW capture → More → Edit RAW** is offered only for
a stored `.sub` under the Sub-GHz folder and a compatible, non-plugin RAW Edit
1.9 FAP at the existing path. The scene checks again before launching;
memory pressure, missing files, wrong FAP/API and failed pack restoration
must not start the editor. The loader queues the exact file path and the
return to Sub-GHz; the old app frees its radio and memory before the handoff.

The editor accepts the saved-file route separately from its existing browser
mode. Direct loading checks the path, RAW header, protocol, sample and line
bounds; it does not run decoded-key conversion. No capture is written during
open or edit; Save is always explicit and uses the existing create-new path.
The editor now checks its largest free heap block and copies only the old
capacity when resizing. This local precaution does not replace or modify the
system allocator. There is no new transmit path, public API or feature ABI.

The two earlier frequency picker additions, long-name handling and the
bounded TextInput change are documented in [the earlier audit](Upstream20261121.md).
Community audit #214 remains unresolved independently of this native app
build. No protected baseline or external binary is accepted here.

On-device acceptance (not host simulation): on a disposable saved capture,
verify that Edit RAW opens the selected item, Back returns to Sub-GHz, and
an explicit save to a new name does not change the source. Open RAW Edit
from its menu too. With an older editor installed, the new item should not
appear. Verify long Rename and 317.000/336.100 MHz receive selection separately;
verify no changes to default hopping or transmission policy.
