# TumoVM PoC

This application is the feasibility gate for
[Tumoflip issue #60](https://github.com/squazaryu/tumoflip/issues/60). It loads a
bounded program from SD and exposes one persistent byte array through NFC
ISO14443-4A and an app-local USB CCID interface.

The PoC is intentionally not a general smart-card runtime. It proves package
loading, bytecode validation, independent transport sessions, shared state,
bounded APDU handling, persistence, and transport cleanup.

The USB interface temporarily uses `076B:3A21`, an identity recognized by the
macOS in-box CCID driver. This is a prototype compatibility measure, not a
stable Tumoflip USB product identity.

## Paths

```text
/ext/apps/Module One/Labs/tumovm_poc.fap
/ext/apps_data/tumovm_poc/packages/shared_object/program.tvm
/ext/apps_data/tumovm_poc/packages/shared_object/state.tvs
```

The default package is created on first launch. Replacing `program.tvm` changes
the route table and bytecode without recompiling the FAP. Invalid program or
state files fail closed and keep NFC/USB transports disabled.

## Default APDU contract

Select the TumoVM application:

```text
00 A4 04 00 05 F0 54 56 4D 01
```

Read four bytes at offset zero:

```text
00 B0 00 00 04
```

Write `AA BB` at offset two:

```text
00 D6 00 02 02 AA BB
```

Successful commands end with status `90 00`. NFC and USB keep independent
selection state but read and update the same persistent object.

## Controls

- Launching the app starts NFC listener and USB CCID together.
- The screen shows transport state, last instruction/status, counters, and the
  first six state bytes.
- Back stops NFC, removes CCID callbacks, restores the previous USB mode, and
  persists pending state.

## Hardware acceptance

1. Launch `Apps -> Module One -> Labs -> TumoVM PoC`.
2. Verify `NFC:ON USB:ON` and no `SAVE!` indicator.
3. On macOS, run `python3 tools/tumoflip/tumovm_ccid_smoke.py`; it discovers the
   reader, verifies SELECT/READ/UPDATE, and restores the original bytes.
4. With an ISO14443-4A reader, repeat SELECT/READ and confirm the USB-written
   bytes are visible over NFC.
5. Exit with Back and verify qFlipper/normal USB and the stock NFC app still
   work.
