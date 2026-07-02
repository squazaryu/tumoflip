# Network/GPS RPC Gate

Tumoflip does not currently ship the upstream Unleashed Network and GPS RPC
services from `687375d7b`.

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

Do not enable `gps_start`, `network_start`, `rpc_gps`, or `rpc_network` as a
drive-by upstream merge.
