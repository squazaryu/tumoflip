# TumoKey

TumoKey is the dev-only FIDO2/U2F authenticator tracked by Tumoflip issue
[#173](https://github.com/squazaryu/tumoflip/issues/173).

The application supports:

- FIDO2/WebAuthn registration and authentication;
- legacy U2F;
- USB HID and NFC ISO-DEP transports;
- local user-presence confirmation;
- ClientPIN and retry lockout;
- up to 32 resident credentials;
- credential inspection, deletion, and destructive reset on Flipper;
- atomic credential and policy-state persistence.

This port is based on the audited GPL-3.0-or-later ZeroFIDO source. See
`UPSTREAM.md` and `THIRD_PARTY_NOTICES.md` for provenance and attribution.

## Security boundary

TumoKey runs on general-purpose Flipper Zero hardware without a certified
Secure Element. It is not equivalent to a certified hardware security key.
Keep another sign-in method for every account used during dev testing.

Credential private keys and PIN state are wrapped with the device-unique
Flipper crypto-enclave key. Metadata required for credential discovery remains
visible on the SD card. Modified firmware, SWD access, live RAM inspection, and
physical device compromise are outside the software-only protection boundary.

Do not use TumoKey as the only authenticator for a production account while the
application remains on the dev channel.

## Daily use

1. Open `Apps -> Module One -> Security -> TumoKey`.
2. Select USB for a Mac or NFC for an iPhone.
3. Start passkey/security-key registration or sign-in on the host.
4. Verify the operation and relying-party name on Flipper.
5. Approve only the request you initiated.

Back must always stop the active transport and restore the previous USB/NFC
configuration.

## Developer verification

Run the bounded native protocol suites with ASan/UBSan:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
python3 applications_user/tumokey/tools/run_protocol_regressions.py
```

Create an isolated host-probe environment:

```sh
python3 -m venv applications_user/tumokey/.tmp/host-venv
applications_user/tumokey/.tmp/host-venv/bin/python -m pip install \
  -r applications_user/tumokey/host_tools/requirements.txt
```

With TumoKey open in USB HID mode, list the FIDO interface and run
non-mutating transport checks:

```sh
PYTHONPATH=applications_user/tumokey \
applications_user/tumokey/.tmp/host-venv/bin/python \
  applications_user/tumokey/host_tools/ctaphid_probe.py --cmd list

PYTHONPATH=applications_user/tumokey \
applications_user/tumokey/.tmp/host-venv/bin/python \
  applications_user/tumokey/host_tools/ctaphid_probe.py --cmd getinfo
```
