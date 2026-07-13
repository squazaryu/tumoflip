# TumoNet Phase A

TumoNet Phase A is a bounded, one-device validation of the packet and session
core proposed in issue #61. It uses the Flipper Zero internal CC1101 and the
Module One external CC1101 as two logical endpoints.

This phase validates real packet-mode RF in both directions, but it does not
claim independent-node range, clock, power, reboot, or physical-security
acceptance. Those checks remain in the parent issue.

## Wire format

All multi-byte integers use network byte order. A CC1101 packet contains:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | Magic `TN` |
| 2 | 1 | Version (`1`) |
| 3 | 1 | Flags (`DATA` or `ACK`) |
| 4 | 1 | Source node ID |
| 5 | 1 | Destination node ID |
| 6 | 4 | Session ID |
| 10 | 4 | Monotonic sender counter |
| 14 | 2 | Message ID |
| 16 | 1 | Fragment index |
| 17 | 1 | Fragment count |
| 18 | 1 | Payload length |
| 19 | 0-24 | AES-CTR ciphertext |
| ... | 16 | Truncated HMAC-SHA256 tag |

The maximum encoded frame is 59 bytes, below the CC1101 64-byte FIFO limit.
Messages are bounded to six 24-byte fragments (144 bytes).

## Session security

- Pairing creates a fresh 256-bit master secret with the hardware RNG.
- The master secret is never written to SD and is cleared when the app exits.
  It remains in RAM while the explicit bench pairing is active so repeated
  scenarios use the same bounded session.
- Independent encryption and authentication keys are derived for each
  direction with HMAC-SHA256 labels.
- Encryption is AES-128-CTR. Authentication is encrypt-then-MAC with a
  128-bit truncated HMAC-SHA256 tag over the header and ciphertext.
- The CTR nonce binds the session, source, destination, sender counter,
  message, fragment, and flags. A sender counter is never reused in a session.
- Authentication is verified before plaintext is accepted or replay state is
  changed.
- Each endpoint keeps a 32-packet replay window. Exact retries are recognized
  as duplicates so the receiver can acknowledge them without redelivery.

This is an experimental protocol. It has not received independent
cryptographic review and must not be used to protect critical data.

## Dual-radio bench

The FAP acquires `SubGhzRadioBrokerDeviceDual`, powers the external module,
loads the same low-power GFSK packet preset into both radios, and runs one of
these bounded scenarios:

- clean delivery;
- first-attempt loss and retry;
- duplicate delivery suppression;
- replay rejection;
- ciphertext/tag corruption rejection followed by retry;
- wrong-key rejection;
- interrupted reassembly cleanup.

The selected frequency must pass both the hardware validity check and the
firmware transmit policy. Stop and Back join the worker before radio devices
are deinitialized, return both radios to sleep, remove external power, and
release the broker lease.

Reports under `/ext/apps_data/tumonet_bench/reports` contain counters and
results only. Keys, plaintext payloads, ciphertext, and authentication tags
are intentionally excluded.
