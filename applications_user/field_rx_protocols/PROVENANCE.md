# Receive-only field protocols

- Import boundary: D4C1-Labs/Flipper-ARF main,
  `9b18419de2774a9693f91a3631653d1538f0f91a`, 2026-10-26.
- Source paths: `lib/subghz/protocols/{x10,urmet,gm_v0,gm_v1,toyota_v0,mazda_v2,mercedes_v0,mercedes_v1,opel_v1,bmw}.{c,h}`.
- License: upstream root LICENSE is GPL-3.0. ARF credits FoxFW and
  @thedoctorz/doctor-firmware for the imported designs/protocols; the latter's
  published LICENSE is also GPL-3.0. Original comments and author attribution
  are retained; this is not a claim of hardware acceptance by those authors.
- No author-source acceptance ledger or protected-package pin is advanced here.

## Selection matrix

| ARF item | Local comparison / decision |
| --- | --- |
| X10 | New32-bit complementary-byte receiver; add receive-only. Fix the upstream bool/status mismatch and validate saved frame bytes. |
| Urmet | New PPM/Manchester-prefix framing; add receive-only with bounded payload/gap loading. |
| GM V0 / V1 | New112-bit frames/checks; existing GM Rolling is a different64-bit receiver. Keep both identities. |
| Toyota V0 | New96-bit payload/120-bit framing, not the existing67/68-bit Toyota A/B/C. Add receive-only. |
| Toyota V1 | Same A/B/C family already present locally. Keep our exact-length, saved-tail and C validation; no parallel alias decoder, renamed "Prius" assumption or looser saved-file loading. |
| Mazda V1 | Receiver feed/reset and wire timing match the local Mazda V0 after variant-name normalization; byte-parity and popcount parity are equivalent. Keep our existing name/files and checks, not a second identical receiver. |
| Mazda V2 | Different packet/field logic; import RX helpers only, not the unused TX lookup tables. |
| Mercedes V0 / V1 | New112/122-bit receivers. Add the common decoder prefix to V0 and preserve the second data word explicitly in files. |
| Opel V1 | New93-bit framing with a captured64-bit cipher; preserve existing public-default key decoding, never search keys. Private common-free helper is replaced locally, not added to firmware exports. |
| BMW | New128-bit framing, not the existing64-bit BMW CAS4 receiver. Preserve both data words and keep reception only. |

## Boundaries / required gates

Encoder functions/structures/descriptors are not imported. Every new protocol has
`encoder=NULL` and no Send flag. No new key database, brute-force, Seed Capturer,
enrollment, jammer or duplicate Cars/Garage application is included. The existing
Standard/ARF brokers and lazy protocol packs remain the only radio owners.

Mechanical source extraction is only a starting point. Every FAL must pass actual
F7 compilation/import checks and native malformed-input, saved-data round-trip
and bounded-noise tests before delivery. Live receiver/remote acceptance remains
pending on the owner's own hardware; no radio is operated by these host tests.
