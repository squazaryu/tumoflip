# Unit tests {#unit_tests}

## Intro

Unit tests are special pieces of code that apply known inputs to the feature code and check the results to see if they are correct.
They are crucial for writing robust, bug-free code.

Flipper Zero firmware includes a separate app called [unit_tests](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/debug/unit_tests).
It is run directly on Flipper devices in order to employ their hardware features and rule out any platform-related differences.

When contributing code to the Flipper Zero firmware, it is highly desirable to supply unit tests along with the proposed features.
Running existing unit tests is useful to ensure that the new code doesn't introduce any regressions.

## Running unit tests

To run the unit tests, follow these steps:

1. Compile the separate test firmware: `./fbt -j2 COMPACT=1 DEBUG=0 FIRMWARE_APP_SET=unit_tests updater_package`.
2. Flash the firmware using your preferred method, including SD card resources (`build/latest/resources`).
3. Launch the CLI session and run the `unit_tests` command.

**NOTE:** To run a particular test (and skip all others), specify its name as the command argument.
Test names match application names defined [here](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/debug/unit_tests/application.fam).

This is a diagnostic firmware profile, not the normal Dev firmware or a FW
Packages group. Building it does not install it on a device. Flash it only as a
separate, explicitly approved hardware test, and restore normal firmware with
its resources afterwards. A successful compile is not a passing on-device run.
Keep the physical C2 reserve of at least 4096 bytes and validate the actual DFU
ranges, not just file size. `DEBUG=0` also means `furi_assert()` is disabled;
input validation must use real error handling rather than debug assertions.

After building and before any device installation, run
`toolchain/current/bin/python3 tools/tumoflip/validate_unit_profile.py --expect-commit "$(git rev-parse HEAD)"`.
It checks DFU ranges, updater CRCs, the exact declared test-plugin set, resource
bytes and every FAL import against the real core test host plus public API.
Private UL-AES crypto and Kia V1 code is linked into its test FAL, not added to
the resident firmware or public SDK. A separate read-only CI job performs the
same build/check; it neither flashes hardware nor publishes test images.

## JavaScript test boundary

JS Runner and its modules are intentionally excluded in Tumoflip. The core
test resolver cannot import private functions from that excluded FAP. The old
JS suite, scripts, helper module and typings are preserved byte-for-byte with
source hashes under `tools/tumoflip/fixtures/deferred_js_tests/`; they are not
compiled, installed, executed or counted as passing core tests. Re-enabling
them requires a separately reviewed out-of-core JS host, not restoring JS to
the resident firmware. Do not infer JavaScript coverage from core test results.

## Adding unit tests

### General

#### Entry point

The common entry point for all tests is the [unit_tests](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/debug/unit_tests) app. Test-specific code is packaged as a `PLUGIN` app placed in a subdirectory of `tests` in the `unit_tests` mother-app and referenced in the common `application.fam`. Look at other tests for an example.

#### Test assets

Some unit tests require external data in order to function. These files (commonly called assets) reside in the [unit_tests](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/debug/unit_tests/resources/unit_tests) directory in their respective subdirectories. Asset files can be of any type (plain text, FlipperFormat (FFF), binary, etc.).

### App-specific

#### Infrared

Each infrared protocol has a corresponding set of unit tests, so it makes sense to implement one when adding support for a new protocol.
To add unit tests for your protocol, follow these steps:

1. Create a file named `test_<your_protocol_name>.irtest` in the [assets](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/debug/unit_tests/resources/unit_tests/infrared) directory.
2. Fill it with the test data (more on it below).
3. Add the test code to [infrared_test.c](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/debug/unit_tests/tests/infrared/infrared_test.c).
4. Build and install firmware with resources, install it on your Flipper and run the tests to see if they pass.

##### Test data format

Each unit test has three sections:

1. `decoder` — takes in a raw signal and outputs decoded messages.
2. `encoder` — takes in decoded messages and outputs a raw signal.
3. `encoder_decoder` — takes in decoded messages, turns them into a raw signal, and then decodes again.

Infrared test asset files have an `.irtest` extension and are regular `.ir` files with a few additions.
Decoder input data has signal names `decoder_input_N`, where N is a test sequence number. Expected data goes under the name `decoder_expected_N`. When testing the encoder, these two are switched.

Decoded data is represented in arrays (since a single raw signal may be decoded into several messages). If there is only one signal, then it has to be an array of size 1. Use the existing files as syntax examples.

##### Getting raw signals

Recording raw IR signals is possible using Flipper Zero. Launch the CLI session, run `ir rx raw`, then point the remote towards the Flipper's receiver and send the signals. The raw signal data will be printed to the console in a convenient format.
