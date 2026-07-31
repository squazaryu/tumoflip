# Network/GPS RPC Gate

Tumoflip does not ship the upstream Unleashed Network and GPS RPC services from
`687375d7b` unchanged.

Those services add always-started firmware records, new RPC/protobuf message
families, and exported firmware API for external FAPs. The firmware-side code is
only a bridge: real network requests and location data must be supplied by a
trusted companion implementation.

The current Tumoflip companion contracts are App Bridge, Tumoflip Runtime, and
package install/verification. Network and GPS are therefore gated until there is
a matching companion-side design.

## Requirements To Enable

- Define the companion protocol and protobuf compatibility story.
- Add explicit iOS permission UX for location and network access.
- Make failure modes visible when the companion cannot provide network or GPS.
- Review privacy boundaries, especially location transport and arbitrary HTTP
  or WebSocket requests.
- Measure firmware RAM/flash impact from the two new services and RPC handlers.
- Verify existing App Bridge, Tumoflip Runtime, BLE readiness, and package
  install flows on hardware.

Issue #255 introduces a narrower first implementation through the existing
authenticated FAB2/FAB3 bridge:

- one explicit iPhone location request while `Flipper Companion` is open;
- one bounded HTTPS GET to an allowlisted public host;
- default-off permissions and visible denial/error states in TumoCompanion;
- no raw sockets, WebSocket, arbitrary credentials, autonomous background
  access, or SD write-through.

Do not enable `gps_start`, `network_start`, `rpc_gps`, or `rpc_network` as a
drive-by upstream merge. The public FAP API and official protobuf adapter remain
gated until the narrower transport is accepted on hardware.
