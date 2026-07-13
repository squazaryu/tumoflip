# TumoVM Peripherals

TumoVM Peripherals turns reviewed SD manifests into bounded virtual devices. It
does not load native code or arbitrary USB descriptors. Version 0.1 supports a
fixed USB consumer-HID descriptor and the existing TumoVM state-v1 NFC Type 4
state machine.

## Package layout

```text
/ext/apps/Module One/Labs/tumovm_peripherals.fap
/ext/apps_data/tumovm_peripherals/packages/*.tper
/ext/apps_data/tumovm_peripherals/state/<package-id>.tvs
```

The runtime loads at most four 1024-byte manifests. Package IDs, API range,
adapter, action, AID, state size, and explicit-action policy are validated
before hardware activation. Unknown adapters and actions fail closed.

## Reference packages

- `usb_media.tper` creates a standard USB consumer-HID device. It can send only
  the declared Play/Pause action and only after the user presses `Send`.
- `nfc_state.tper` creates an ISO14443-4A endpoint with the existing bounded
  SELECT/READ/UPDATE TumoVM profile. State writes are persisted atomically.

Only one package can own a transport at a time. Starting USB temporarily
replaces the normal USB interface; stopping, Back, and app exit release all HID
keys and restore the previous interface. Stopping NFC closes its listener and
persists pending state before returning ownership.

## Controls

- `Prev` / `Next` select an SD package.
- `Start` validates and activates it.
- USB: `Send` emits one allowlisted consumer action.
- NFC: `Reset` clears only the current selection session, not persistent state.
- `Stop` releases the active transport.
- `Info` shows the selected adapter, action, version, and project issue.
- Back always stops the active adapter before returning to Cockpit.

## Acceptance

1. Start `USB Media Key`, connect Flipper USB to a host, and verify `Host
   connected`. `Send` should toggle media playback once. `Stop` must restore
   qFlipper/normal USB.
2. Start `NFC State Token` and run the existing iPhone `TumoVM NFC Smoke`.
   SELECT/READ/UPDATE/RESTORE must pass. `Reset`, repeated scans, and Back must
   not wedge NFC.
3. Replace an adapter/action/API field with an unknown value and confirm the
   package is shown as blocked without changing USB or NFC state.
