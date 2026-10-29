# USB CD-ROM — Tumoflip managed adaptation

Imported from `xMasterX/all-the-plugins` at
`ac2138f0502039cf215b4bdbb7d12e7a9fe9d2b3`, directory
`non_catalog_apps/usb_cdrom`. Author project: `piersroberts/flippercd`, observed
`095a0909f664ec60089f758d2c6bba5f91c4f601` (0.1.2). These are different
provenance boundaries, not an assertion that both source trees are identical.
GPL-3.0 license is preserved and embedded in the delivered FAP assets.

Local 0.2 changes: package-only Base route `apps/USB/usb_cdrom.fap`, zero-initialized
application/USB ownership state, unsigned big-endian address decoding, checked
32-bit transfer length and strict BOT command-wrapper fields. Images open with
FSAM_READ; there is no SCSI tag/device writer, image write or format operation.
The existing USB profile is restored on normal exit. Nothing is auto-launched
by the firmware and no ISO image is shipped in the package.

Select an ISO using the app. USB RPC/qFlipper access is temporarily unavailable
while the CD-ROM profile owns USB; Back exits and restores the prior profile.
Image size must be a nonzero multiple of 2048 bytes and at most 4 GiB. A request
whose byte count cannot fit the USB transport is rejected, not wrapped to zero.
Large ISO transfers use the Flipper's USB/SD throughput; this is not a claim of
PC-speed optical emulation, host boot compatibility or tested physical USB timing.

Host tests execute the production SCSI implementation under fatal ASan/UBSan.
F7 FAP build/import checks pass separately. Windows/macOS/Linux host attachment,
eject/Back/unplug and concurrent BLE use remain physical acceptance items.
