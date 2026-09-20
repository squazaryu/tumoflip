# BLE Remote device selection and Toyota C — unreleased

Only these two changes are in scope. No new menu styles, broad ARF migration,
Bluetooth Central, new encoder or key-recovery feature is included.

## Bluetooth → Bluetooth Remote

The existing `hid_ble.fap` becomes a **package-only** FW Packages Base item at
`/ext/apps/Bluetooth/hid_ble.fap`; it is no longer bundled in updater resources.
The app ID/data directory remain unchanged. Its existing `.bt_hid.keys` and
`.bt_hid.cfg` are reused without resetting pairing credentials. USB Remote
(`hid_usb`) remains separate and bundled; BadUSB and the Community Kodi remote
are not moved or modified.

Startup always opens **Device** before advertising. The last selected row is
remembered/highlighted, but the user confirms the target for each Remote session.
This prevents a previously paired phone connecting before the user chooses a PC.
Selection replaces the controller accept/resolving lists with only that bonded
identity, including private-address resolution. A missing host stays **Waiting**;
failed selection keeps advertising stopped, never falls back to unrestricted mode.

- **Name selected** gives a local ASCII label of up to 12 characters. Unknown
  names initially show the complete compact Bluetooth address; OS device names
  are not supplied by the controller's bond table.
- **Forget selected** removes just that bond and acknowledges only after the
  remaining pairing data is persisted. A failure can leave controller/storage
  state uncertain and is explicitly reported for rechecking.
- **Add device** is explicit open pairing, started from the host's Bluetooth
  settings. Disconnect other previously paired hosts during this step; open
  pairing is not a selected-device session. Done/Back stops it before choosing
  the new host. No nearby-device scanner is added.
- Existing whole-table unpairing remains an explicit confirmed action and no
  longer automatically starts unrestricted advertising afterwards.
- Companion uses its own profile/key store. Remote disconnects it while active
  and restores the default profile on exit; concurrent Companion+HID is not added.

Selection/labels are journalled separately in two bounded CRC-checked preference
records (`.bt_hid.targets.a/b`). Short writes, sync/close/readback errors are not
success. Interrupted preference writes retain another valid record. No pairing
secrets are copied into these records. The BLE FAP uses a 4 KiB stack for its
bounded preference I/O; the USB FAP retains its original stack setting.

F7 API advances additively from **88.10 to 88.11** for four BT-service calls:
`bt_profile_start_idle`, `bt_get_bonded_devices`, `bt_set_connection_peer`, and
`bt_forget_bonded_device`. Existing GapConfig/profile ABI and default advertising
behavior are preserved. Controller-list helpers remain private. This FAP needs
the matching new firmware; a matching API major alone is not sufficient.

Independent package allowlist, protected `hid_ble` identity/data family and
Companion protection are maintained in companion worktrees. The latest inspected
Community Pack (`19sep2026`) contains no regular `hid_ble` entry; protecting its
canonical filename also prevents a future renamed catalog entry overwriting it.
`btremote_kodi`, `hid_usb` and `bad_usb` are deliberately not added to this policy.

## Toyota C

Narrow receive-only adaptation from ARF
`976a03f299fcfad7b4464102b3352de1be825883`. The shared A/C preamble is resolved by
the sync gap rather than routing every former A frame to C. Original A/B reception
and tests remain. C is bounded to 66–68 bits; truncated/oversized frames and
implausible field combinations are rejected. Detection is not vehicle identity,
authentication or a verified CRC claim.

New saved captures carry `ToyotaVariant`; C also stores `ToyotaTail` so the bits
beyond the normalized 64-bit Key survive save/reopen. Old A/B files infer their
variant from the existing bit count. Invalid explicit variants/tails fail closed.
The Toyota protocol remains decoder-only (`encoder = NULL`). No TX behavior is
added to Standard or ARF. Use the matching rebuilt protocol package.

## Verification and delivery gate

Tests execute production peer-policy, preference-storage and scene/event code
with host/controller/storage failures. `measure_ble_peer_coverage.py` separates
production coverage from adapters. Both BLE and USB FAPs compile/import-check;
release CI now includes both and the new regression suites. BadUSB source and
the underlying HID report/profile implementation are unchanged.

Before publication: final clean F7/SDK/paired-package validation, corresponding
Companion protection build and independent catalog revision are required.
The existing `009-003` suffix is only a development baseline, not permission to
overwrite its published files. No new release or physical acceptance is implied.

Hardware checks remain pending:

- Previously paired iPhone plus Mac/Windows PC: choose PC with the phone nearby;
  verify only PC connects, absent PC waits, sleep/wake/private-address changes,
  switching and exiting back to Companion.
- Add a new host, cancel pairing, label peers, forget exactly one, restart Remote,
  and test SD removal/errors without corrupting existing bonds.
- Every existing Remote mode and USB Remote smoke check.
- Own Toyota A/B/C recordings, mixed/noisy input, Standard/ARF/Auto Decode,
  save/reopen and unchanged encoder availability.
