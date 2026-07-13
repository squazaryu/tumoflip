# Protocol Compiler

Protocol Compiler is the Flipper-side validator for receive-only `.tproto`
profiles. The heavy inference step runs on a host with
`tools/tumoflip/protocol_compiler.py`; the FAP only loads bounded profiles and
validates saved Sub-GHz RAW captures deterministically.

Initial schema limits:

- API 88 only;
- pulse-pair OOK encoding;
- up to 64 data bits and 24 preamble pulses;
- up to 512 pulses per Flipper-side validation;
- profiles must contain both `Receive only: true` and `Review required: true`;
- no transmit or radio ownership path exists in this FAP.

Host workflow:

```sh
python3 tools/tumoflip/protocol_compiler.py compile \
  --name "My Profile" \
  --output my_profile.tproto \
  capture_1.sub capture_2.sub capture_3.sub

python3 tools/tumoflip/protocol_compiler.py validate \
  --profile my_profile.tproto held_out.sub
```

Install profiles in `/ext/apps_data/protocol_compiler/profiles/`. A packaged
demo profile and held-out capture are included in Tumoflip FW Packages.
