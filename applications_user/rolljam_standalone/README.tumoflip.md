# RollJam Shield Receiver source

Tumoflip keeps this RollJam Shield Receiver in-tree as an experimental source
import, but it is not the canonical `RollJam` entry in ARF Sub-GHz Full.

The app keeps `appid=rolljam_standalone` so its protocol plugins retain their
existing dependency graph. If it is built manually, its FAP is routed under
`/ext/apps_data/rolljam_standalone/` and its protocol and emulate plugins are
loaded from `/ext/apps_data/rolljam_standalone/plugins`; it must not overwrite
`/ext/apps_data/arf_subghz_full/modules/rolljam.fap`.

The canonical ARF Sub-GHz Full `RollJam` child is the classic RollJam app from
`applications_user/rolljam`, packaged as
`/ext/apps_data/arf_subghz_full/modules/rolljam.fap`.

Use only with devices you own or are explicitly authorized to test.
