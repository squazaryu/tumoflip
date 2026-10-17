# Nice O-Code 72-bit and transient Garage Door Remote settings

Sources reviewed on 2026-10-17:

- Unleashed `7e26e042b48a1e6173ba95c265733d2b145b46ca` and formatting
  follow-up `f62964cb8216c566cb3940992d0fea6bbe91d840`.
- ARF `35930e59a8f0e518637932da9c4a3f91e5571f2a` / `2044b722c08102bd591adb9ac795275f3ad0b606`
  and Community source `13edbe6303e99070957e3d18298635310fb87118`.

## Adapted

- Append Nice O-Code One to Standard Add Manually without renumbering old IDs.
  Basic and advanced generation keep the 72-bit choice and known installer code.
- Keep the old 52-bit API intact; add the 72-bit generator with parcel 3's
  matching Nice One tail. Local public API becomes **88.21** (upstream's 88.20
  is a different symbol set). Private Sub-GHz feature ABI remains **1003**.
- Nice O-Code capture/export retains the long-frame shape and rebuilds the
  supported 52/72-bit frame using the captured counter/button and resolved serial.
  A received 112-bit frame is exported as supported 72-bit, not transmitted as
  112-bit. Original capture deserialization and unknown-IC TX guards remain.
- In our Garage Door Remote FAP, move the settings codec into an embedded FAL
  loaded only for load/save, then released immediately. Keep the existing file
  format and paths, defaults, dual-RX/shield values and radio-broker architecture.
  Missing/wrong plugins leave defaults in RAM and inhibit saving those defaults.
- Detach the receiver settings view before releasing it on exit. Re-entry uses
  the existing lazy allocator. Null/failed allocation exits are safe.

## Deliberately not copied

- Pandora CPU/keyfob emulation, firmware upload, unlock sequences or brute force.
- ProtoPirate's whole host/config ABI and duplicated Standard UI.
- No protocol/frequency changes were found in the reviewed ARF range.
- No new installer-code recovery algorithm or fresh-serial `New` action.
- No saved signal deletion, clean-up action, SD migration or device flashing.

## Verification and delivery boundary

Native tests use synthetic data and mocked storage/plugin/GUI boundaries, with
ASan/UBSan. The encryption primitive is substituted in the new framing test;
it tests framing/CRC/input and write-error handling, not hardware RF acceptance.
Existing 52/72/112 receive/deserialize/unknown-IC tests remain in the host suite.
The new test module is included in PR and release CI.

Firmware and matching FAP/FAL files must be built together for API 88.21.
No iOS implementation change is required. A separate FW Packages catalog
revision is not implied by this source change; its published inventory must be
checked at release time. Nothing here promotes Stable or authorizes ESP32 flash.

Hardware acceptance still needs: own Nice remote/receiver, missing rainbow table,
52/72 save/reopen, normal FloR-S/Nice One, and repeated GDR settings entry/Back
with both valid and unavailable plugin assets.
