# TumoSpectrum

TumoSpectrum is Tumoflip's standalone signal research workspace. It can map a
selected Sub-GHz band, launch the stock RAW recorder at a selected carrier,
build a bounded capture set, infer stable and changing fields, and decode a live
Sub-GHz stream with a selected receive-only `.tproto` profile.

## Band Map and Smart Capture

`Band Map` is a bounded receive-only scanner:

- 64 frequency bins and eight waterfall rows;
- noise-floor estimate and decaying peak history;
- cursor tuning, x1/x2/x4 zoom, hold and peak snap;
- internal CC1101 or a detected external CC1101;
- four RSSI samples per 100 ms UI tick, with no busy polling loop.

Controls:

- Left/Right: move the frequency cursor;
- Up: change band;
- Down: switch Internal/External radio;
- OK: stop the scanner and open the stock Sub-GHz RAW recorder at the cursor;
- long Up: change zoom;
- long Down: snap the cursor to the strongest retained peak;
- long OK: hold/resume scanning.

After the stock recorder saves a RAW file, TumoSpectrum validates it and adds it
to a Smart Capture session. The session stores at most four file paths in
`/ext/apps_data/signal_workbench/band_map_session.ff`; source `.sub` files are
never rewritten. Three compatible captures are enough to run inference and
create a live profile. A completed profile clears only the session list, not the
RAW captures.

## Protocol profiles

Profiles can be built directly on Flipper from three or four compatible RAW
captures. The host compiler remains useful for batch work and independent
validation:

```sh
python3 tools/tumoflip/protocol_compiler.py compile \
  --name "My Profile" \
  --output my_profile.tproto \
  capture_1.sub capture_2.sub capture_3.sub

python3 tools/tumoflip/protocol_compiler.py validate \
  --profile my_profile.tproto held_out.sub
```

Install profiles in `/ext/apps_data/signal_workbench/profiles/`, then open
`Protocol Profiles` in TumoSpectrum. The live path owns the radio through Radio
Broker v2, decodes on the Flipper, and writes changed observations to
`/ext/apps_data/signal_workbench/protocol_observations.csv`.

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

Band Map and profile runtime do not transmit. Explicit handoff is exposed only
for a set classified as static-like, and the stock Sub-GHz app remains
responsible for user confirmation and regional transmission rules. TumoSpectrum
does not implement brute force, jamming, rolling-code replay or unattended
transmission.
