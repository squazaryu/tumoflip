# Tumoflip Claude Buddy

Tumoflip Claude Buddy turns Flipper Zero into a physical remote and status display
for Claude Code. It provides haptic, audio and LED feedback, forwards button actions
to the host bridge, and keeps a compact event transcript on the device.

This variant is based on
[`jxw1102/flipper-claude-buddy`](https://github.com/jxw1102/flipper-claude-buddy)
v0.6 and keeps its connection/disconnection and memory-leak fixes.

## Tumoflip differences

- Uses the standard serial Bridge transport for both USB and BLE.
- Keeps the iOS Companion passthrough contract working over BLE.
- Shows host event text in the on-device Transcript view.
- Shows the latest session token status on the main screen.
- Removes the selectable Claude Desktop/NUS mode to avoid conflicting BLE profiles.
- Ships through Tumoflip FW Packages as `/ext/apps/Bluetooth/claude_buddy.fap`.

The unused NUS source modules remain vendored to make future upstream comparison
straightforward, but the runtime always selects `BleModeBridge`.

## Controls

| Button | Action |
|---|---|
| Up | Start or stop voice dictation |
| Up (hold) | Hold Space for voice input |
| Left | Interrupt Claude (Esc) |
| Left (hold) | Send Ctrl+C |
| Right | Open slash-command menu |
| Right (hold) | Open info menu |
| OK | Submit Enter |
| OK (hold) | Type `yes` and submit |
| Down | Send Down arrow |
| Down (hold) | Toggle mute |
| Back | Send Backspace |
| Back (hold) | Exit |

## Host side

Tumoflip uses the Companion/relay bridge already configured for the firmware. The
upstream Claude Code plugin may still be used for terminal integration, but its FAP
must not replace this Tumoflip build.
