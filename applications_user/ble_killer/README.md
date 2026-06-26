# BLE Killer

Tumoflip packages this ARF app as an isolated opt-in external app under
`/ext/apps/ARF Tools/ble_killer.fap`.

This app is a UART controller for an external BLE-capable device. It is not part
of Flipper's built-in BLE stack, BLE App Bridge, or the iOS companion path.

Use only in a controlled lab setup with devices you own or are explicitly
authorized to test. The app should launch and exit safely even when the external
UART device is not connected.
