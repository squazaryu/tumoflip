# Compact firmware profile

Tumoflip keeps the firmware core in internal flash and uses the SD card for
optional applications and package overlays.  The compact profile is a small,
reversible build-time optimization for the internal image; it is not a second
runtime loader and it does not move the boot path, HAL, storage, updater,
standard NFC or standard Sub-GHz implementation to the SD card.

## What stays in every release build

- boot, recovery, updater and the firmware API boundary;
- HAL, power, USB/BLE peripheral bridge, storage and the application loader;
- normal NFC, LF RFID, infrared and Sub-GHz operation, including the radio
  broker and TX policy;
- the public diagnostic/bridge entry points used by TumoCompanion.

These are the parts that must work before an SD card is mounted or an external
FAP is launched.

## What the compact profile omits

`TUMOFLIP_ROADMAP_FULL` defaults to `0`.  The release image therefore omits
optional engineering surfaces that were consuming flash and a small amount of
static RAM:

- the runtime trace ring and its mutex;
- persistent crash/reset journal and radio-session CSV export;
- extended runtime diagnostics (heap snapshots, manifest details and the
  non-destructive hardware-check suite);
- radio protocol capability/session-history export metadata;
- passive BLE scan and GATT-client implementation in the GAP glue.

The public symbols remain present.  Disabled surfaces return a deterministic
empty/unsupported result (`trace=0`, `depth=0`, or `false`) instead of silently
pretending that a check ran.  The compact capability payload advertises
`diag=0;rc=0;rs=0;rp=0`, so Companion can hide those optional actions without
probing a missing endpoint.  Existing NFC, Sub-GHz/ARF, BLE peripheral
connection and external FAP launch paths are not replaced by stubs.

The full engineering profile is available for diagnostics and development:

```sh
FBT_TOOLCHAIN_PATH=/path/to/toolchain/current \
  ./fbt -j2 --extra-define=TUMOFLIP_ROADMAP_FULL=1 firmware_all
```

It must remain buildable even though it is not the default release image.

## Flash and RAM accounting

Flash and RAM are different constraints.  Moving code to an external FAP can
save internal flash, but the FAP still needs loadable RAM while it runs and
the SD card adds an availability/failure path.  For that reason this change
removes only optional resident diagnostics and metadata first; it does not
remove NFC or radio protocols that are part of the product contract.

## Why standard protocols are not moved to the SD card

Standard Sub-GHz protocols in `lib/subghz/protocols` are linked through the
core registry used by the receiver, transmitter, file decoder and CLI.  NFC
implementations (including MIFARE Classic, Ultralight AES, DESFire, EMV and
FeliCa) are similarly coupled to the poller/listener state machines and the
NFC application's protocol-support registry.  They must be available before
the SD card is mounted and during recovery/error paths.

Moving those implementations would require a versioned dynamic protocol ABI,
an SD-backed registry, bounded relocation/loading buffers, dependency checks,
and a safe fallback when the card is absent or corrupt.  That is a new loader
architecture, not a file move.  It would also move execution pressure from
flash to heap: an active FAP is loaded into RAM and can fragment the heap or
increase startup latency.  A missing SD card could then turn ordinary NFC or
Sub-GHz use into a runtime failure.

The safe external boundary already exists for optional ARF/FAL protocol packs
and large application FAPs.  They are loaded on demand and can fail without
removing the standard radio/NFC path.  Any future dynamic protocol work should
start with one non-essential ARF module behind a versioned ABI and a fallback,
then be measured on-device for heap high-water mark, fragmentation, startup
latency and SD removal.  It must not be used for the standard protocol
registry, HAL, updater, storage or loader.

The two profiles were measured with the same toolchain and target:

| profile | `.text` | `.rodata` | `.data` | `.bss` | free flash |
| --- | ---: | ---: | ---: | ---: | ---: |
| compact (release default) | 703,716 B | 175,440 B | 740 B | 7,460 B | 168,340 B |
| full (engineering) | 713,136 B | 178,552 B | 740 B | 7,460 B | 155,812 B |

The compact image is 12,532 bytes smaller in flash than the full profile.  The
linker's static `.bss` is unchanged because the removed buffers are allocated
inside service objects; their heap reduction is real at runtime, but should be
confirmed with the device's `max heap block` readout during acceptance.

The release gate uses the physical ELF layout and the updater's C2 boundary.
`updater_package` must pass with a non-negative C2 gap; a successful build with
an overlapping C2 image is not a releasable artifact.

## Validation

The static contract tests validate both capability payloads.  The compact
payload advertises `trace=0` and excludes the trace feature, while the full
payload keeps `trace=1` and the complete engineering feature set.  This prevents
the app from treating a deliberately omitted diagnostic surface as a failed
hardware check or as a firmware regression.
