# TumoFlow

TumoFlow runs bounded, foreground-only field workflows on Flipper Zero. It
consolidates Tumo Macro Deck and TumoScript while keeping both legacy formats
readable during migration.

## Controls

- `Up` / `Down`: select a workflow.
- `Left`: validate.
- `Right`: action preview.
- Hold `Right`: dry-run without hardware output.
- `OK`: arm and run.
- Hold `OK`: import a legacy `.tmacro` or `.tscr` into TumoFlow without deleting
  the original.
- Hold `Left`: edit a native workflow trigger. Reference-based IR and Sub-GHz
  triggers remain file-edited so their paths cannot be discarded accidentally.
- `Back`: cancel the active wait/action or leave the app.

## Format

Native workflows live in `/ext/apps_data/tumoflow/workflows` with the `.tflow`
extension.

```text
# TumoFlow v1
name Field Demo
trigger manual
policy stop
log Workflow started
delay 250
prompt Press OK
bridge event field_demo
log Workflow done
```

Supported triggers are `manual`, `countdown <ms>`, `ir <path>|<signal>`,
`subghz <path>`, `nfc`, `lf_rfid`, and `ibutton`.

Supported actions include `log`, `delay`, `prompt`, `bridge`, bounded
`gpio_pulse`, bounded `ir_burst`, saved `ir_file`, approved static
`subghz_file`, labels, branches, and `goto`.

## Safety

TumoFlow has no daemon, shell, native code loader, brute-force mode, rolling
code generation, or unbounded loop. It allows at most 32 actions, 64 executed
steps, eight output actions, 15 minutes of runtime, one-second GPIO activity,
500 ms raw IR, and a single static Sub-GHz repeat. Output workflows require an
explicit arm confirmation and always release NFC, IR, RFID, iButton, GPIO, and
Sub-GHz resources on exit.
