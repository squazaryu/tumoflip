# Upstream e6 Sub-GHz adaptation

This branch adapts Unleashed commit [`e6ded9b`](https://github.com/DarkFlippers/unleashed-firmware/commit/e6ded9b108454b30a3e710295e6d2b7fd146e188) and its required follow-up [`3431d19`](https://github.com/DarkFlippers/unleashed-firmware/commit/3431d19d5270b531ed620acaca0aac4b57f26061).

## Included

- Nice O-Code support in the shared Nice Flor-S implementation, plus the `nice_o_code` installer-code recovery FAP.
- Security+ 2.0 86-bit keypad frames with optional `Pin` field and the `secplus_pin` helper FAP.
- A separate dynamic 42-bit Prastel rolling-code protocol. The legacy CAME decoder no longer claims those frames.
- KeeLoq learning variants for JCM Gen2, Stagnoli, and table-driven Telcoma entries, with the matching extended encrypted manufacturer-key data.

The protocol registry is shared by Standard and ARF. Both hosts, Companion/Quac/Wardriving, the JavaScript Sub-GHz module, raw editor, and the ARF KeeLoq key browser load the extended data after the existing Tumoflip keystore.

## Compatibility decisions

The upstream commit replaces the encrypted `keeloq_mfcodes` file and assumes a newer upstream protocol tree. Tumoflip has custom manufacturer entries and a different encryption IV, so the original file is deliberately preserved. The upstream table is shipped as `keeloq_mfcodes_extended`; loading it is additive and read-only. User entries still come only from `keeloq_mfcodes_user`.

The upstream follow-up fixes the Nice rainbow-table path bound and makes the Security+ encoder check its fixed buffer capacity. Public helper exports bump the F7 API from 88.5 to 88.6; all bundled FAPs must be rebuilt against that API.

## Validation status

- `python3.11 -m unittest tools.tumoflip.test_upstream_e6_protocols` — passed.
- Full F7 `updater_package` build with `fap_nice_o_code` and `fap_secplus_pin` — passed locally.
- Standard and ARF source paths are covered by static contracts and compile checks.
- Physical acceptance is still required for owned remotes: Nice O-Code recovery, Security+ keypad/PIN read/write, Prastel capture/replay, and each new KeeLoq learning path. No hardware result is inferred from the build.
