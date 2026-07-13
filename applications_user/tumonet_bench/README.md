# TumoNet Bench

TumoNet Bench is the one-device Phase A validation app for TumoNet. It treats the
Flipper Zero internal CC1101 and the external Module One CC1101 as two bounded
radio endpoints and exercises the real RF path in both directions.

The pairing key exists only in RAM and is erased when the app exits. Reports contain
configuration and counters only; keys, plaintext, ciphertext, and authentication tags
are never written to storage or logs.

This app proves protocol, crypto, retry, replay, fragmentation, and radio lifecycle
behavior. It does not replace the two-independent-device acceptance gate on issue #61.
