# Specter adaptation

Vendored from `xMasterX/all-the-plugins` commit
`8970b6ba0e48a9c63b4bfaf07ca121f5b08e5b34`, path `non_catalog_apps/specter`,
Specter 3.0 by at0m-b0mb. Original project:
https://github.com/at0m-b0mb/Specter-FlipperZero. MIT license retained in LICENSE.

Tumoflip 3.1.0 changes: package-only delivery; preflight full log-entry capacity;
check sync/close; best-effort rollback of a partial append; report failure if
either TXT or CSV output fails. The two files are not a transactional filesystem:
on CSV failure a complete TXT entry may remain. Never claim both were saved.

Tumoflip 3.3.0 selectively adapts Specter 3.1.1 from the original author's
`v3.1.1` tag at `079474ba10c54fc7f9e23d4f896a2982dda0a64e` (MIT):
canonical-scale proximity and feedback in Duty % mode, a bounded/skippable
startup intro, and migration of the existing v2 settings file without losing
preferences. This is not a wholesale replacement of the Tumoflip 3.2.0
implementation; its transactional log append and package-only route remain.

This app observes external 13.56 MHz carrier presence only. It does not transmit,
identify a device's intent, or prove a room safe/unsafe. CLEAN means no field was
detected under the selected sensitivity, not absence of surveillance equipment.
Physical NFC, calibration, cancellation, intro navigation and SD-removal tests
remain pending.
