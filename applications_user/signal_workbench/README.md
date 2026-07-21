# TumoSpectrum

TumoSpectrum is Tumoflip's standalone signal research workspace. It can capture
or open Sub-GHz and IR RAW files, compare bounded capture sets, infer stable and
changing fields, and decode a live Sub-GHz stream with a selected receive-only
`.tproto` profile.

## Protocol profiles

The heavy profile inference step remains a host tool:

```sh
python3 tools/tumoflip/protocol_compiler.py compile \
  --name "My Profile" \
  --output my_profile.tproto \
  capture_1.sub capture_2.sub capture_3.sub

python3 tools/tumoflip/protocol_compiler.py validate \
  --profile my_profile.tproto held_out.sub
```

Install profiles in `/ext/apps_data/signal_workbench/profiles/`, then open
`Protocol Profiles` in TumoSpectrum. The live path owns the internal CC1101
through Radio Broker v2, decodes on the Flipper, and writes changed observations
to `/ext/apps_data/signal_workbench/protocol_observations.csv`.

Existing profiles from `/ext/apps_data/protocol_compiler/profiles/` are copied
to the canonical directory on first launch. The legacy source is not deleted.

`.tproto v1` limits are intentionally conservative:

- API 88;
- receive-only pulse-pair OOK;
- up to 64 data bits and 24 preamble pulses;
- up to 512 pulses per decode window;
- both `Receive only: true` and `Review required: true` are mandatory;
- no transmit path or protocol-registry injection.

TumoSpectrum's separate multi-capture inference remains bounded to 96 inferred
bits. Decoded Sub-GHz key files do not contain the original pulse stream and are
not valid inputs for profile training.
