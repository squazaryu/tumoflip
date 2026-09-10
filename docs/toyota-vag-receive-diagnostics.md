# Toyota framing and VAG receive diagnostics

Selective adaptation of ARF f8467b3f / 0b95e88b (Toyota framing)
and 13eb4e1b (VAG receive flags).

Toyota requires complete 68-bit A / 67-bit B frames. The A preamble
must end in a valid data pair, so a foreign synchronization gap cannot
silently begin a frame without contributing a bit. B data pulses must fit
the existing short/long tolerances. Original timing windows and minimum
preamble lengths are retained; no guessed serial, hopping-code or button
allowlist rejects otherwise structurally valid frames.

Standard, RAW Auto Decode and ARF consume the shared Toyota decoder.
The separate ProtoPirate registry does not contain this Toyota decoder.

Both VAG decoder implementations retain the low nibble of the decrypted
button byte as diagnostic BtnFlags and display it on a separate short line.
Raw keys remain authoritative on reopening: flags are recomputed from the
decoded payload, not trusted from edited metadata. Reset clears prior flags.
ProtoPirate capture copying retains the optional BtnFlags field.

Existing button representations (core high nibble, ProtoPirate's own convention),
encoder behavior, key material and counters are unchanged. Flags are diagnostic
data, not a confirmed vehicle window-control feature.

Host tests execute the production parser against synthetic valid A/B signals,
truncated frames, foreign sync/pulses and recovery/repeated use. The regression
fails on the previous Toyota source. Both VAG field extractors are tested for
all 256 possible button-byte values while preserving their previous outputs.
Real remote/capture acceptance remains pending: check genuine Toyota signals
and short/held VAG captures through Standard/ARF and ProtoPirate save/reopen.
