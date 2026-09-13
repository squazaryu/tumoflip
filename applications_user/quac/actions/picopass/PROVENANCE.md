# Quac Picopass clean-room provenance

This directory is licensed under **GPL-3.0-or-later**; see `LICENSE.txt` and
the repository root `LICENSE`. Relevant authors and acknowledgements are in
`AUTHORS.md`.

Permitted source references used for this implementation:

- Tumoflip firmware `9598136346b8b691dd3eefa85623e53dcf1eacb2`, including
  Quac 0.9.3 integration and the API 88.4 NFC transport.
- Official `flipperdevices/flipperzero-firmware`
  `08bafc478e98f6d179e059759cc13d9bf199a151`, under the repository GPLv3,
  for the saved-file format and protocol constants.
- `RfidResearchGroup/proxmark3`
  `47d0a8fa00ee53d3f93c9fe00fb1a44b8180e3d3`, GPL-3.0-or-later, as the
  secondary protocol and non-secure state-machine reference.

Tumoflip's action, parser, transport adapter, tests and UI wiring were written
for this repository. No Loclass, MAC, authentication, diversification, recovery,
dictionary, or card-write implementation is included.

The parser accepts only non-secure page mode, unencrypted application blocks,
and placeholder Block 3/4 values. Any secure/encrypted mode or non-placeholder
key block returns `Unsupported secure card` before the listener is allocated.

No third-party icon is included. Quac reuses its existing NFC icon.
