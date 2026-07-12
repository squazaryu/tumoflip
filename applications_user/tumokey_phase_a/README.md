# TumoKey Phase A

Experimental, dev-only USB CTAPHID prototype for issue #64.

Phase A implements bounded 64-byte HID framing, one fixed-capacity transaction,
`CTAPHID_INIT`, `CTAPHID_PING`, `CTAPHID_CANCEL`, and
`authenticatorGetInfo` over `CTAPHID_CBOR`. It uses the existing FIDO HID USB
interface and restores the previous USB configuration on exit.

This build does not create, store, import, sign with, or enumerate credentials.
It does not advertise resident keys, Client PIN, user verification, or
credential algorithms. Its AAGUID and device version are synthetic test values.
Do not use it as an authenticator for any account.

The wire contract follows the official FIDO CTAP 2.2 specification:
<https://fidoalliance.org/specs/fido-v2.2-ps-20250714/fido-client-to-authenticator-protocol-v2.2-ps-20250714.html>

Host validation:

```sh
python3 -m unittest tools.tumoflip.test_tumokey_phase_a
```
