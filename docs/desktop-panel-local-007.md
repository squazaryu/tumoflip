# Local Panel candidate based on Dev 011-006

This is an unpublished local test candidate, not a Stable release. The source
starts at released 006 (`950d58388b5574421c27a733562a671e8ec40b3c`). It deliberately
does not merge rejected 005 or change its boot, RTOS, recovery, journal or resident
library compilation policy. Hardware acceptance remains pending.

## Included

- A paired GUI redraw callback/context binding, with deterministic native
  attach/detach race regressions. Update producers must still stop before freeing
  callback contexts; Panel contexts live for the whole Desktop service lifetime.
- Optional `Settings -> Desktop -> Desktop view -> Panel`. Classic is the default.
- Three native pages: SD/Bluetooth-enabled/free-RAM status; auto-lock/sleep holds;
  the existing retained fault record, if readable. Bluetooth **On** is not a claim
  of a working phone/RPC connection. An unavailable fault record does not mean
  that the device has never crashed.
- Short Left/Right changes pages; held Left/Right/OK uses an independent Panel
  shortcut profile. Short OK opens Apps. Other controls stay unchanged.
- Optional 160 ms page motion. No decorative idle timer, allocation per frame,
  network, device commands, file enumeration or filesystem I/O in draw callbacks.
- Versioned, verified dual-bank private configuration. Public DesktopSettings v21
  remains 648 bytes and API stays 88.23. Old 005 panel files are ignored, not
  deleted. New data lives in `/int/.desktop_cards2.0` and `.1`.
- Diagnostic wording/labels and storage CLI help shortened to retain the 4096-byte
  C2 reserve without dropping functionality. Runtime C tokens and printf argument
  order/types were checked against the pre-compaction checkpoint. Thread names
  are preserved. Log labels may differ; use the actual protocol name where known.

## Local acceptance (owner performs installation)

1. Keep the known-working 006 installer and back up the SD before installation.
   Install the **complete updater**, not a bare DFU; it includes matching Desktop
   Settings resources. No cleanup or movement of user data is required.
2. First boot must show the normal Classic desktop. Check menu, PIN/lock/unlock,
   one ordinary app launch/return and the existing phone connection.
3. Enable Panel in Desktop settings. Check all three pages, both directions and
   page wrap. Test motion On/Off, repeated rapid presses, then a held shortcut.
4. Assign different Classic/Panel held shortcuts. Change modes and verify that
   one profile does not overwrite the other. Check the first held shortcut after
   waking the backlight, then lock/unlock and app-return again.
5. Leave Panel idle until the backlight turns off. Wake it, restart normally and
   verify the saved mode. Test SD-present/absent status only with the card safely
   removed through the ordinary device UI; do not remove it during writes.
6. With Panel selected, hold Back during a normal startup: Classic is forced for
   that boot, without rewriting the stored selection. This is not DFU/recovery.

Stop immediately on a crash, reboot loop, stuck input or failed lock transition.
Do not retry flashing the same failing candidate repeatedly. Record the visible
message and stage, and roll back to the working 006. Local/native tests cannot
prove physical boot, LCD timing, battery drain or long-idle stability.

Nothing in this task publishes a tag/catalog, updates iOS or flashes hardware.
