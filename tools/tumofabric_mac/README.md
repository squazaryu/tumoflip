# TumoFabric Mac

Native macOS USB participant for the bounded TumoFabric Counter runtime.

## Build and test

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test
./script/build_and_run.sh --verify
```

The app discovers `/dev/cu.usbmodemflip_*` and exclusively opens the Flipper
CLI port. Close qFlipper before using the app. Quit TumoFabric Mac before
reopening qFlipper.

The app sends only the fixed `tumofabric` command grammar. It does not expose a
general terminal, execute host shell commands, or persist BLE session tokens.
