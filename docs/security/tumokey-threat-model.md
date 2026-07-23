# TumoKey Threat Model

Status: implementation review gate for issue #173; dev-only credential testing is approved,
but stable or sole-authenticator use is not.

## Overview

TumoKey is an experimental USB/NFC roaming authenticator for Tumoflip. The
current implementation ports and adapts ZeroFIDO commit
`56b433f9d1390ad224246c85a06083b40de4903e`, preserving its GPL notices and
auditable internal symbol names. It provides bounded CTAP HID/CBOR handling,
U2F, FIDO2 MakeCredential/GetAssertion, discoverable credentials, ClientPIN,
credential management, local approval, and interrupted-write recovery.

TumoKey is not a certified authenticator and must not claim protection
equivalent to a YubiKey or a Secure Element-backed product. The Flipper crypto
HAL explicitly states that the device was not designed or audited as a secure
platform, can be dumped through a debugger or modified firmware, and cannot
guarantee secret safety. The device-unique enclave key can improve at-rest
protection against casual SD-card inspection, but it does not establish a
tamper-resistant authenticator boundary.

The implementation remains a separate dev-only FAP and storage namespace. It
does not modify or replace the stock U2F application. The earlier Phase A FAP
remains packaged only as a fallback until real USB/NFC acceptance is complete.

Primary references:

- `applications/main/u2f/`: existing U2F APDU, HID, UI, and storage code.
- `targets/f7/furi_hal/furi_hal_usb_u2f.c`: 64-byte FIDO HID transport.
- `targets/f7/furi_hal/furi_hal_crypto.c`: RNG, enclave-backed AES, and GCM.
- `targets/furi_hal_include/furi_hal_crypto.h`: platform security limitations.
- `applications/services/storage/storage.h`: sync, rename, and storage APIs.
- FIDO CTAP 2.2: <https://fidoalliance.org/specs/fido-v2.2-ps-20250714/fido-client-to-authenticator-protocol-v2.2-ps-20250714.html>
- WebAuthn Level 3: <https://www.w3.org/TR/webauthn-3/>

### Security objectives

TumoKey must preserve these properties within its explicitly limited threat
model:

1. A USB host cannot obtain credential private keys through CTAP responses,
   logs, reports, backups, or normal storage access.
2. A credential is used only for its bound RP ID and only after the required
   local authorization gesture.
3. TumoKey never reports user presence or user verification unless that event
   was completed for the current request and has not expired.
4. Corrupt, truncated, replayed, oversized, or malformed input fails closed
   without crashing the firmware, resetting secrets, or silently creating a
   new authenticator identity.
5. Interrupted writes preserve either the previous complete vault or the new
   complete vault. A partially written vault is never accepted.
6. PIN and credential-management state cannot be reset by replacing only the
   removable SD card.
7. Secret buffers are bounded, have minimal lifetime, and are explicitly
   zeroized before release.
8. The stock U2F app, normal USB mode, and unrelated FAP workflows continue to
   work after TumoKey exits or fails.

## Threat Model, Trust Boundaries, and Assumptions

### Assets

- Per-credential P-256 private keys and any key-wrapping root material.
- Credential IDs, RP ID hashes, user handles, display names, and resident
  credential metadata.
- PIN verifier, PIN/UV tokens, retry counters, lockout state, and reset state.
- Signature counters or any replacement anti-cloning state.
- User-presence and user-verification decisions for the active ceremony.
- Authenticator identity, AAGUID, attestation policy, and vault schema version.
- Availability of USB, storage, Bluetooth core2, and the normal Flipper UI.

### Actors

- Owner: controls the Flipper display and buttons and may connect it to hosts.
- Honest client: browser or operating system implementing WebAuthn and CTAP.
- Malicious USB host: sends arbitrary HID frames and CTAP/CBOR payloads, races
  requests, withholds packets, disconnects, and attempts consent confusion.
- Malicious RP: supplies deceptive RP/user strings and repeated ceremonies
  through an otherwise conforming client.
- Storage attacker: reads, edits, truncates, replaces, or rolls back the SD
  card while the Flipper is powered off.
- Physical/debug attacker: controls SWD, installs modified firmware, inspects
  RAM, or replaces the whole device. This actor is not resisted by the
  software-only design and can recover or misuse credentials.
- Developer/supply-chain actor: changes vendored parser or crypto code, build
  inputs, packages, or release artifacts.

### Trust boundaries

1. **USB host to HID reassembly.** Every packet, CID, sequence number, length,
   command, timeout, and cancellation is attacker-controlled.
2. **HID message to CTAP/CBOR dispatcher.** CBOR types, nesting, map keys,
   byte-string lengths, algorithm identifiers, and extension data are
   attacker-controlled.
3. **Request dispatcher to consent UI.** The UI must bind the displayed RP and
   operation to one immutable pending request. A later USB request must not
   reuse an earlier button press.
4. **Application RAM to crypto enclave/AES.** Slot 11 protects a device-unique
   key from normal reads, but derived keys and credential plaintext exist in
   CPU1 RAM during use and are exposed to modified firmware/debug access.
5. **Application to removable SD storage.** SD contents are untrusted. Encryption
   without integrity, freshness, and atomic commit is insufficient.
6. **Application to internal storage.** Internal files are harder to replace
   accidentally than SD files but are not tamper-resistant or monotonic under
   a physical/debug attacker.
7. **TumoKey to global USB configuration.** TumoKey temporarily owns the FIDO
   HID interface and must restore the previous interface on every exit path.
8. **Build and package boundary.** The shipped FAP, API 88 metadata, vendored
   CBOR dependency, and firmware package must match the reviewed source.

### Assumptions and explicit limitations

- USB HID and NFC ISO-DEP are enabled for dev testing. BLE is outside this
  issue. NFC acceptance requires repeated iPhone ceremonies and transport
  cleanup checks before this FAP can replace the Phase A route.
- The Flipper is a single-user authenticator. Anyone who can operate the
  unlocked device may satisfy a presence-only request.
- The user can read the display and distinguish the requested operation and RP
  before confirming it.
- Device loss, malicious firmware, SWD access, invasive extraction, and live
  RAM capture defeat the software-only authenticator. These are product
  limitations, not acceptable claims of resistance.
- Slot 11 may be used only as a root for domain-separated key derivation. User
  enclave slots 12-100 are not allocated because the public HAL warns that
  their availability is not stable for public applications.
- No private credential export, cloud sync, Companion backup, diagnostic dump,
  or migration format is permitted.
- Attestation is `none`, self-attestation, or a device-local packed certificate.
  TumoKey must not reuse the stock U2F batch certificate or imply FIDO
  certification or a protected model identity.
- Signature counters may be reported as zero if rollback-resistant monotonic
  storage cannot be guaranteed. A rollbackable counter must not be presented
  as clone detection.

## Attack Surface, Mitigations, and Attacker Stories

### HID transport and request lifecycle

Attacker story: a hostile host sends fragmented, interleaved, oversized, or
out-of-order HID frames to overwrite memory, exhaust the worker, or authorize a
different request than the one on screen.

Target stable controls:

- Reuse the FIDO HID descriptor, but implement CTAPHID commands in a separate
  bounded transport module.
- Cap the declared `maxMsgSize`; allocate no request-sized buffers from
  untrusted lengths; reject lengths before copying.
- Track one active request per channel with strict sequence, timeout, INIT,
  CANCEL, and KEEPALIVE behavior. A new request invalidates pending consent.
- Fuzz frame reassembly and cancellation on the host with ASan/UBSan before
  hardware testing.
- Return protocol errors for malformed input. Never use `furi_check` or
  `furi_crash` on host-controlled values.
- Restore the previous USB interface after normal exit, parser failure,
  storage failure, and cancellation.

### CBOR and CTAP command parsing

Attacker story: a host uses deep nesting, duplicate keys, integer overflow,
indefinite lengths, or unsupported algorithms to corrupt state or produce an
ambiguous signed response.

Required controls:

- Vendor a small maintained CBOR implementation or a separately reviewed
  bounded parser. Do not parse CTAP maps with ad hoc byte scanning.
- Pin the dependency revision and record its license and source digest.
- Enforce limits for nesting, map entries, arrays, text, byte strings, total
  message size, and credential lists.
- Reject duplicate required keys, invalid UTF-8 where text is required,
  trailing bytes, unsupported COSE algorithms, and non-P-256 keys.
- Encode signed authenticator data deterministically and test it against
  independent host-side decoders.

### User presence, RP binding, and consent confusion

Attacker story: a malicious host displays one RP, races in another request,
then consumes a stale OK press to create or use the wrong credential.

Required controls:

- Copy the validated RP ID and operation into an immutable pending-request
  object before displaying the confirmation screen.
- Show `Create`, `Sign in`, `Delete`, or `Reset`, plus a safely truncated RP ID.
  Generic `Press OK` text is not sufficient for TumoKey.
- Bind one short OK press to one request nonce and expire it after a short,
  visible timeout. Back always rejects.
- Clear presence/verification state after success, error, cancellation,
  disconnect, timeout, or screen exit.
- Destructive reset and credential deletion require a separate local screen
  and a long confirmation gesture.

### Credential vault and power-loss safety

Attacker story: an SD card is copied, modified, truncated, or replaced with an
older vault to expose metadata, reset policy state, or force unsafe recovery.

Required controls:

- Use a versioned binary envelope with magic, schema, generation, nonce,
  ciphertext length, and authentication tag. These header fields are AEAD AAD.
- Derive a domain-separated vault wrapping key from the device enclave, then
  use AES-GCM or an independently reviewed authenticated construction.
- Keep at most a small fixed credential capacity; reject storage-full without
  deleting or overwriting an existing credential.
- Write a temporary file, sync it, verify by reopening and authenticating it,
  then rename it into place. Maintain a recoverable previous generation.
- Treat authentication failure, unknown schema, and inconsistent generation as
  `Vault corrupt`; do not auto-reset or generate a new identity.
- Keep PIN retry/lockout state in internal storage with a journal and integrity
  check. SD-only retry state is prohibited. This still does not resist modified
  firmware and must be documented as such.
- Zeroize wrapping keys, PIN material, private keys, ECDH secrets, and decrypted
  records with `mbedtls_platform_zeroize` on every exit path.

Current dev implementation:

- Each credential uses a versioned bounded record. Private credential material
  and HMAC-secret values are wrapped with the device enclave; discovery
  metadata such as RP and account labels remains visible on the SD card.
- Record, counter, PIN, and policy writes use temporary files plus recovery
  paths, and native tests cover interrupted-write recovery and corruption.
- PIN retry state has a sealed integrity structure, but it is still software
  state and does not provide a hardware monotonic counter.
- The complete record is not an AES-GCM vault. This is an explicit stable gate,
  not a property that the current dev build claims to satisfy.

### Client PIN and user verification

Attacker story: a host brute-forces a PIN, rolls back retry state, retains a
PIN/UV token, or tricks TumoKey into setting the UV bit after presence only.

Required controls:

- Do not advertise `clientPin` or `uv` in `authenticatorGetInfo` until the full
  selected CTAP PIN/UV protocol and negative tests are implemented.
- Use the CTAP-specified ECDH, encrypted PIN fields, token permissions, and
  constant-time authentication checks. Do not invent a PIN protocol.
- Rate-limit attempts, persist retries before returning an error, invalidate
  tokens on disconnect/reset/timeout, and lock safely on write failure.
- Never log PIN hashes, shared secrets, PIN/UV tokens, or plaintext PINs.
- Do not store discoverable credential names that expose account identity until
  local user verification is implemented. User handles alone still require
  encrypted and integrity-protected storage.

### Cryptography, randomness, and key lifecycle

Attacker story: weak random keys, nonce reuse, cross-protocol key reuse, or
stale secrets in RAM allow credential recovery or forgery.

Required controls:

- Use the STM32 hardware RNG. The current `furi_hal_random_fill_buf` API returns
  no status and can wait indefinitely on a persistent hardware fault, so Phase
  B requires a bounded checked RNG API before generating credential keys.
- Treat an unavailable crypto enclave, Bluetooth core2, or checked RNG failure
  as a fatal initialization error. Never fall back to a PRNG or fixed seed.
- Use P-256/ES256 only in the first version. Validate scalar and point handling
  with known-answer and malformed-key tests.
- Separate derivation labels for vault encryption, metadata integrity, PIN
  state, and non-resident credential handles.
- Never use factory-shared enclave slots for user credential protection.
- Generate each credential key independently. Never derive credential keys
  directly from RP text or a low-entropy PIN.
- Do not retain decrypted vault contents for the entire app lifetime when a
  smaller scoped operation is possible.

### Privacy, logging, and diagnostics

Attacker story: diagnostics, crash logs, screen text, or Companion exports leak
which accounts and RPs are registered.

Required controls:

- Logs contain only command IDs, bounded error categories, and aggregate
  counts. No RP IDs, user names, user handles, credential IDs, hashes, or key
  bytes.
- Runtime Trace, Diagnostics Export, package-state, and Companion protocols
  must not read the TumoKey vault or enumerate credentials.
- Credential management displays only after successful user verification and
  must avoid leaving account data in global UI history.
- FAP deletion must not silently export or preserve plaintext recovery data.

### Availability and coexistence

Attacker story: a malformed request deadlocks USB, exhausts RAM, corrupts the
vault, or leaves qFlipper and other USB apps unavailable after exit.

Required controls:

- Fixed-capacity state machines and bounded stack use; no recursion from CBOR.
- Timeouts for incomplete HID messages, consent, crypto, and storage writes.
- Cancellation checks between expensive crypto and storage phases.
- A failed request cannot mutate the vault unless the complete operation and
  atomic commit succeed.
- Hardware acceptance includes repeated open/close, disconnect during each
  command, SD removal, low-space, and power interruption tests.

### Development phases and release gates

| Phase | Allowed capability | Gate |
| --- | --- | --- |
| A | Dev-only FAP, CTAPHID INIT/PING/CBOR framing, `authenticatorGetInfo`, parser tests | No credential persistence; malformed USB corpus must not crash |
| B | Test-account non-discoverable ES256 credentials with UP | Wrapped key handles, RP binding, consent UI, zeroization, browser smoke |
| C | Discoverable credentials and credential list/delete | Atomic records, capacity handling, corruption and power-loss tests |
| D | Client PIN and UV-required operations | CTAP PIN/UV protocol, sealed retry state, rollback analysis, negative tests |
| E | Candidate stable integration | Independent security review, conformance subset report, hardware acceptance; no unresolved Critical/High findings |

The port implements Phase B-D capabilities for disposable test accounts, but
capability presence is not equivalent to passing those release gates. Real
credentials must not be primary or sole account credentials before Phase E.
BLE, backup/export, multi-device sync, and Secure Element support are outside
this issue.

## Severity Calibration (Critical, High, Medium, Low)

### Critical

- USB input causes code execution or arbitrary memory write in the authenticator.
- Any normal host, SD reader, log, backup, or API can recover credential private
  keys or a reusable PIN/UV token.
- TumoKey signs for an RP other than the RP bound to the selected credential.
- User verification is reported without successful verification for the active
  request.

### High

- A stale or raced button press authorizes a different make/get/delete/reset
  operation.
- SD replacement resets PIN retries or converts a locked authenticator into an
  unlocked one.
- Vault corruption silently resets identity, drops credentials, or accepts
  attacker-modified records.
- Interrupted writes routinely destroy the only valid credential vault.
- Private material remains in diagnostics or persistent crash output.

### Medium

- A malicious host can persistently deny service until app restart without
  compromising secrets.
- RP/account metadata is exposed without required user verification.
- USB mode is not restored after an error, requiring a reboot.
- A standards mismatch causes safe authentication failure across common clients.

### Low

- Non-sensitive status text, icon, or timeout behavior is inconsistent.
- A bounded malformed request causes a recoverable per-request error with no
  secret, state, or cross-application impact.
- Test-only tooling leaks synthetic credential identifiers that cannot be used
  outside the test corpus.

## Decision

**Conditional GO for dev-only Phase B-D hardware testing.** The separate
TumoKey FAP may create and use credentials only on disposable test accounts
while USB HID, NFC, PIN, credential-management, interruption, and cleanup
acceptance is performed. Automatic approval is compiled out, diagnostics are
compiled out, and every ceremony still requires a local Flipper decision.

The current public firmware API provides `furi_hal_random_fill_buf()` without a
success/failure result. A bounded checked RNG API is therefore still absent.
TumoKey must fail its stable security gate until this is resolved or an
independently reviewed platform guarantee is accepted. The current per-secret
enclave wrapping also does not satisfy the stronger complete AES-GCM vault
target above.

Stable-channel inclusion is explicitly denied until an independent security
review, real-client conformance subset, checked-RNG decision, and storage
confidentiality/integrity decision have passed. The Guide/About screen labels
TumoKey as a development build and states that it has no Secure Element and
must not be the user's only authenticator.

Repository: squazaryu/tumoflip
Upstream: MinorGlitch/zerofido@56b433f9d1390ad224246c85a06083b40de4903e
