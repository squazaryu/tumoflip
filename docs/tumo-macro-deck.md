# Tumo Macro Deck

Tumo Macro Deck is a Module One FAP for local, SD-backed action sequences.

Macros live in:

```text
/ext/apps_data/tumo_macro_deck/macros/*.tmacro
```

Each run writes a CSV log to:

```text
/ext/apps_data/tumo_macro_deck/runs/run_YYYYMMDD_HHMMSS.csv
```

## Format

```text
# Tumo Macro Deck v1
name Safe Demo
policy stop
log Macro started
delay 250
ble_event safe_demo_started
wait_button Press OK
log Macro done
```

Supported safe steps in this increment:

- `log <text>` writes a local run-log entry.
- `delay <milliseconds>` waits in cancelable chunks.
- `wait_button <prompt>` waits for `OK`; `Back` cancels the run.
- `ble_event <payload>` emits `tumo_macro_deck/event` through App Bridge v2
  with legacy fallback.

Hardware-output steps are recognized but gated:

- `ir <args>`
- `gpio <args>`

They require `OK` confirmation, then return `unsupported` and follow the macro
policy. This keeps the parser, UI, cancellation, logging, and policy behavior
testable before enabling physical output.
