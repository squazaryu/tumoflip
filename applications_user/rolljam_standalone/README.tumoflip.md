# RollJam 2.0 ARF module

Tumoflip packages this ARF RollJam 2.0 app as the canonical RollJam child module
under `/ext/apps_data/arf_subghz_full/modules/rolljam.fap`.

The app keeps `appid=rolljam_standalone` so its protocol plugins retain their
existing dependency graph, but it is no longer shipped as a second visible
`ARF Tools` app. Protocol and emulate plugins are distributed separately under
`/ext/apps_data/rolljam_standalone/plugins` instead of being embedded in the
startup FAP, keeping the RollJam process small enough to launch reliably from
the ARF Sub-GHz Full hub. The legacy `applications_user/rolljam` source remains
in-tree for reference and is excluded from reproducible updater packages.

Use only with devices you own or are explicitly authorized to test.
