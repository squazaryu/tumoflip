# Passive Nice O-Code RAW analysis

Selective adaptation of Unleashed `5d3c950e98a3504c018e0810eb7faca3540827d8`
and its documentation correction `a7964de2e0c6c1ecd27c96fcac7b9fdc34543a76`,
reviewed on 2026-10-18. This does not import the upstream certificate transmitter
or automatic enrollment flow.

## User path

Open a saved RAW file in **Standard Sub-GHz**, then **More → Nice O-Code**.
The action sits beside Auto Decode and Decode Current and passes the same file
to the existing Nice app; there is no second file picker or protocol-pack scan.
Auto Decode itself still uses its established history/pack workflow.

The Standard session finishes and restores its original protocol pack before
queuing the FAP. Loader copies the argument and starts it only after Standard
exits. Missing app resources or a failed pack restoration do not schedule a
handoff. Exiting Nice returns to Standard's start screen, not to an invented
reconstruction of its previous temporary history.

Nice O-Code **1.3** accepts a validated `/ext/subghz/*.sub` RAW file argument.
Its RAW loop and Standard Auto Decode share the bounded sample reader. The
common worker opens decode-only files with `FSAM_READ`; normal TX retains its
previous path. No radio is acquired in offline mode. Header/protocol/frequency,
path traversal and repeated-start checks fail closed.

- At most400 samples per tick; WAIT does not become EOF, oversized gaps reset
  the decoder, and stop joins the worker before freeing receiver/environment.
- The RAW percentage is advisory and capped below100 until EOF. EOF, not an
  animation or percentage, controls transition to analysis/results.
- Existing installer-code analysis is reused unchanged; no broader search or
  automotive PIN/seed feature is added. Four distinct presses are still needed.
- The source RAW is not rewritten. Save is explicit and creates a separate new
  file through the previously added exclusive-create/failure-cleanup flow.

## Framing and passive certificate boundary

- All16 72-bit O-Code parcels use the reviewed k-byte table. Only the final two
  carry the end-of-press flag. Plain Nice One and 52-bit FloR-S retain their
  previous behavior. A redundant 16-entry uint64 parcel-index array was removed.
- Native tests inspect the production encoder's synthetic pulse buffer for all
  16 parcels, both52/72 bits and both ordinary/O-Code modes, with a buffer canary.
- A FAP-local certificate parser validates frame widths, marker/type and both
  CRCs before exposing a24-bit value. Zero is a valid value, not an unknown
  sentinel. The packet must belong to the accepted capture; conflicting values
  are hidden and reported as a conflict.
- `Cert(CRC)` means structural CRC agreement, **not authenticated ownership or
  permission to enroll a transmitter**. Single-bit corruption tests cover all
  112 bits of synthetic frames; no real receiver acceptance is inferred.
- Save may add `Analysis_Cert` to the newly created analysis file. It does not
  write upstream `Cert`/`Cert_Cnt`, create enrollment transmissions, or change
  the existing112-bit TX rejection. Standard's raw `Data2` preservation remains
  intact, including frames that the certificate parser refuses.

## Verification / delivery

- New native tests: framing/CRC, bounded RAW lifecycle, actual decoder worker
  read mode, deferred-launch ordering, missing resources and failed restoration.
- UI renderer uses actual draw code and firmware fonts for12 states, checks text
  widths/frame bounds/footer clearance and runs with ASan/UBSan.
- Native fixture coverage:465/494 lines (**94.13%**, includes mocks; not whole-
  firmware or hardware coverage).
- Public API remains **88.21**, private Sub-GHz feature ABI **1003**.
  Physical C2 reserve remains4096 B; the measured section gap is4208 B.
- No release, Stable promotion, iOS/backend flashing, scheduler change or audit
  disposition update is included. New firmware and matching Nice FAP resources
  must be delivered together in a future Dev build.

Hardware checks still needed: own RAW captures and own remote/receiver,
52/72 transmit regression, corrupted/truncated/empty RAW, Back during reading
and solving, return to Standard and actual SD permission/error behavior.
