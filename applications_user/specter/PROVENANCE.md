# Specter adaptation

Vendored from `xMasterX/all-the-plugins` commit
`8970b6ba0e48a9c63b4bfaf07ca121f5b08e5b34`, path `non_catalog_apps/specter`,
Specter 3.0 by at0m-b0mb. Original project:
https://github.com/at0m-b0mb/Specter-FlipperZero. MIT license retained in LICENSE.

Tumoflip 3.1.0 changes: package-only delivery; preflight full log-entry capacity;
check sync/close; best-effort rollback of a partial append; report failure if
either TXT or CSV output fails. The two files are not a transactional filesystem:
on CSV failure a complete TXT entry may remain. Never claim both were saved.

This app observes external 13.56 MHz carrier presence only. It does not transmit,
identify a device's intent, or prove a room safe/unsafe. CLEAN means no field was
detected under the selected sensitivity, not absence of surveillance equipment.
Physical NFC, calibration, cancellation and SD-removal tests remain pending.
