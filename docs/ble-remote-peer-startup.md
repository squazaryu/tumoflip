# Bluetooth Remote: cold-start readiness and readable peer names

## Scope

Fixes the owner-reported `Cannot select peer / Retry from Devices` and improves
device labels. Based on `560ead5bf37dbd72229b774d18db3617f318b64a` (PR #497).
No pairing reset, key deletion, change to selected-peer filtering, BLE Central
scan, iOS change, or release publication is part of this integration.

## Root cause and correction

The published 009-004 GAP initializer ultimately set `connection_handle=0xFFFF`,
whereas peer selection, explicit pairing and individual forgetting waited for `0`.
An idle HID profile therefore timed out before reaching any controller command.
Earlier service and controller-policy tests used independent mocks and missed the
incompatible initialization value between those components.

The absence value is now consistently `UINT16_MAX` at initialization, matching
disconnection and the three idle checks. Zero remains a valid HCI handle. An idle
state during asynchronous termination is insufficient until the matching disconnect
event invalidates the handle. Timeout, tick wraparound and failed controller policy
remain fail-closed; no fallback to a different host is introduced.

The new native test executes the production initializer, idle wait, selection,
forgetting and disconnect-event branch together. It reproduces the original failure
without contacting hardware or changing any device data.

## Names and interaction

`aci_gap_get_bonded_devices()` returns address type and address, not a host name:
see `Bonded_Device_Entry_t` in the pinned STM32WB `ble_types.h` and the command's
documentation in `ble_gap_aci.h`. We do not infer names or device classes from MACs.

- Unnamed entries display `Device 1`, `Device 2`, etc., without raw address strings.
- Temporary labels are generated from the current list without SD writes and avoid
  collisions with saved names. They are not advertised as stable host identities.
- The first explicit selection opens `Name this device` before any connection or
  preference write. Enter a local name such as `My MacBook`, `Office PC`, or `iPhone`.
- Save persists the label against the exact bonded identity, then selects that peer.
  Back cancels without selecting a new peer or writing preferences.
- Already named devices connect directly. `Rename selected` changes the local label.
- The existing limit remains 1–12 printable ASCII characters; blank, whitespace-only,
  control-character and unterminated names are rejected. The on-device keyboard is
  unchanged. This does not rename the computer/phone or the Flipper's advertised name.
- SD failure prevents connection. A controller rejection may leave the requested name
  saved, but leaves the peer inactive and never opens unrestricted pairing.

The two-slot CRC journal layout (724-byte version-1 records), existing aliases,
`.bt_hid.keys` and `.bt_hid.cfg` are unchanged. No data migration is needed.

## Delivery and validation

The GAP fix lives in firmware; labels live in package-only `hid_ble.fap`, now version
1.3. Updating just FW Packages cannot fix the old firmware readiness check. Deliver
a new Dev build first and matching FW Packages next. F7 API remains locally 88.12;
there is no additional public API change. USB Remote stays bundled and unchanged by
the peer-name UI changes.

Automated coverage includes real GAP cold start, matching/unrelated disconnects,
connection handle zero, timeout/wraparound, controller rejection, name collisions,
identity stability after list reordering, first naming, cancel, invalid names,
SD failure, existing rename/forget/restart and journal persistence. Both PR and
release CI must run `test_ble_gap_lifecycle` alongside the existing peer tests.
`measure_ble_peer_tests` reports suite-union coverage of an explicit set of real
C functions under ASan/UBSan, excluding radio/RTOS/storage mocks; it is not a
whole-firmware or physical-device acceptance claim.

Physical acceptance remains pending:

1. Update the firmware and FW Packages; do not erase or re-pair existing devices first.
2. Cold-launch Bluetooth → Bluetooth Remote. Select an existing unnamed device, name
   it and connect; retry with an already named device.
3. With a phone nearby, select the PC; an unavailable PC must never fall back to the phone.
4. Cancel the naming screen, test an SD save failure, rename an existing device, restart
   the app/device and confirm its label survives and still identifies the same host.
5. Check Add device, normal connection/reconnection, individual forgetting on a test
   pairing, return to Companion and USB Remote smoke tests.
