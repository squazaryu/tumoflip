# FlipperRelay BLE Bridge

`FlipperRelay` is the host-side part of the tumoflip BLE App Bridge workflow.
It lets Flipper apps send small framed BLE events to a paired Mac. The Mac
daemon receives those events and runs only commands that are explicitly listed
in a local JSON allowlist.

This is intended for personal automations such as toggling a local script,
calling a Home Assistant helper, opening a Mac app, or running another command
that you configure on the Mac.

## Firmware Side

tumoflip adds a BLE App Bridge GATT service to the default serial BLE profile.
Flipper apps can send frames with:

```text
app_id + command + optional payload
```

Current senders in this repository:

- `FlipperRelay` app sends `sber_relay` commands: `on`, `off`, `toggle`.
- `Quac` supports `.qab` actions with this format:

```text
app_id|command|payload
```

Example Quac action:

```text
sber_relay|toggle|
```

## Mac Side

Install the Python BLE dependency:

```sh
python3 -m pip install bleak
```

Copy the example config and edit commands for your Mac:

```sh
cp tools/FlipperRelay/commands.example.json tools/FlipperRelay/commands.local.json
```

Run the bridge:

```sh
python3 tools/FlipperRelay/mac_bridge.py --config tools/FlipperRelay/commands.local.json
```

If your Flipper advertises with another name, override the BLE target:

```sh
python3 tools/FlipperRelay/mac_bridge.py \
  --config tools/FlipperRelay/commands.local.json \
  --ble-target TUMOFLIP
```

## Config

The bridge executes only exact `(app_id, command)` mappings from the config.
The Flipper payload is not executed as shell code. It is exposed to the process
as environment variables:

```text
FLIPPER_APP_ID
FLIPPER_COMMAND
FLIPPER_PAYLOAD
```

Example command mapping:

```json
{
  "app_id": "sber_relay",
  "command": "toggle",
  "run": ["/usr/bin/osascript", "-e", "display notification \"Toggle\" with title \"FlipperRelay\""]
}
```

For real automations, point `run` to your own script:

```json
{
  "app_id": "sber_relay",
  "command": "toggle",
  "run": ["/Users/you/bin/toggle-relay.sh"]
}
```

Keep local tokens and private endpoints out of this repository. Put them in
local scripts, environment variables, Keychain, or an ignored local config.

## Flipper Settings

On the Flipper, BLE must be enabled and the App Bridge setting must be enabled:

```text
Settings -> Bluetooth -> App Bridge
```

The Mac must be paired with the Flipper and close enough for BLE notifications
to be delivered reliably.
