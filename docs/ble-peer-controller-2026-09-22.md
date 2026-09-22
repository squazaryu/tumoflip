# Bluetooth Remote controller-path audit — 2026-09-22

Status: software corrections under PR #511; physical acceptance pending.
The user's repeated error is **not** treated as resolved by the earlier host tests.

## Evidence

The former 009-005 fix corrected the absent connection handle to `UINT16_MAX`.
It did not validate the controller-command contract. The existing policy test
made `hci_le_set_address_resolution_enable` succeed and the GAP lifecycle test
replaced `gap_peer_select`, leaving this gap between test layers.

The pinned STM32WB documentation in
`lib/stm32wb_copro/wpan/ble/core/doc/STM32WB_BLE_Wireless_Interface.html`
lists opcode `0x202D` (address-resolution enable) for LO/LB controller-only stacks,
not PO/BF host stacks. The bundled radio is Light/PO 1.20.0, at `0x080D7000`
(`firmware/Release_Notes.html` maps Light to PO and lists Privacy as supported).
Both Select and Add called this unavailable HCI command before supported ACI.
The new native reproducer returns Unknown Command at that exact boundary and
fails on the original production selection function.

Two further contract errors were found:

- `aci_gap_set_discoverable` ignores its filter-policy argument. The selected
  path now uses `aci_gap_set_undirected_connectable(..., 3)` and a bounded payload;
  a payload/controller error cannot open unfiltered advertising.
- Resolving a bonded host's changing RPA needs privacy enabled at GAP startup.
  Only the explicit idle/peer-selection profile opts in, via private core helpers.
  Its enhanced connection event is parsed at the correct field offsets. The
  ordinary profile's configuration, public `GapConfig` layout and FAP ABI stay
  unchanged. Failed connection events do not install a bogus handle.

ST references: [filtering and private addresses](https://wiki.st.com/stm32mcu/wiki/Connectivity:STM32WB-WBA_Filter_Accept_List),
[wireless stack variants](https://wiki.st.com/stm32mcu/wiki/Connectivity:STM32WB_BLE_Wireless_Stack).
These are controller API requirements, not proof of a real host connection.

## Artifact delivery

The Dev 019 manifest and the actual Dev 020 ZIP contain the same Remote 1.3:

- path `apps/Bluetooth/hid_ble.fap`, 108444 bytes;
- MD5 `5b66d44015c0b5bab3948901865cdca2`;
- SHA-256 `5a53b5ad6673df80045343dbd78128be830692176af72df118cdcf8171fcd723`.

The published package did not revert the previous FAP fix. This controller repair
requires new firmware; rebuilding/reinstalling the unchanged FAP alone is not a fix.
The user's installed bytes/version remain unverified until device evidence is available.

## Validation boundaries

Native tests execute the production policy, GAP startup/advertising/event handling,
BT service routing, HAL startup/restore and existing picker/preferences functions.
They cover unknown HCI commands, supported ACI operations, error paths, handle zero,
filtered selection versus explicit pairing, failed/enhanced connection events,
and restoring the unmodified legacy startup path.

Run `python -m tools.tumoflip.measure_ble_peer_tests` for ASan/UBSan and explicit
production-function region coverage. Radio/RTOS/host behavior remains mocked.
No bond database reset, credential deletion, device flash or hardware acceptance
has been performed. Test cold launch, selected host unavailable with phone nearby,
new pairing/cancel, RPA reconnect, rename persistence and return to Companion on
the actual Flipper before claiming the complete fix is accepted.
