# Sub-GHz Radio Broker

The Radio Broker is a system service that owns arbitration state for Sub-GHz
radio clients. It provides:

- an exclusive lease with an owner name;
- external CC1101 power ownership;
- preservation of OTG power enabled by another subsystem;
- detection of an external 5 V rail already supplied by USB VBUS;
- selected internal/external/dual device status;
- observable lifecycle status;
- automatic external-power cleanup when a valid lease is released.

The system Sub-GHz application, Sub-GHz Remote, JS Sub-GHz module, ARF Sub-GHz
Full, Flipper Companion Sub-GHz TX, QUAC Sub-GHz actions, Garage Door Remote,
classic RollJam, the RollJam Shield Receiver source, ProtoPirate, and Sub-GHz
Bruteforcer are Broker clients.
They acquire a lease before
initializing the device registry and release it after stopping the radio and
deinitializing the registry. Core, ARF, Companion, and QUAC `subghz_txrx` helpers
report explicit lifecycle transitions: `probing`, `initialized`, `rx`, `tx`,
`async_rx`, `async_tx`, and `cleaning_up`. External-module probing and fallback
to the internal CC1101 keep their existing behavior.
USB VBUS is treated as available external power without assigning OTG ownership
to the active lease, so cleanup only disables boost that the Broker enabled.

The system Sub-GHz app reports `dual` while `RX Mode: DUAL` is active. Radio
settings keep the preferred module and receive mode as separate controls, and
the Read screen can switch between AUTO and DUAL directly. Dual mode uses
independent workers and decoders for the internal and external CC1101,
deduplicates decoded frames in the core receiver history, and falls back to the
internal radio when the external module is unavailable. Transmission remains
single-radio and prefers the external CC1101 in this mode.

Classic RollJam is the ARF Sub-GHz Full `RollJam` child module. It owns a Broker
lease and routes external CC1101 power through the Broker while retaining its
direct internal-RX/external-TX logic. The RollJam Shield Receiver source and
Garage Door Remote report dual-radio selection for dual/shield receiver modes.
ProtoPirate and Sub-GHz Bruteforcer keep their custom device loaders, but pass
the active lease explicitly so probing, fallback, and cleanup preserve Broker
power ownership.

The remaining direct OTG allowlist entries are IR-only power users
(`flipper_xremote` and QUAC IR output). They do not initialize or operate the
Sub-GHz device registry and are intentionally outside Radio Broker ownership.

Applying a global lease inside
`subghz_devices_init` would turn an existing missing `deinit` into a persistent
radio lock, so migration remains explicit per application.

Runtime `status` includes `radio=<state>` and `owner=<owner>`. This is an
observability contract only; it does not acquire, release, or reconfigure the
radio.

The public API is in
`applications/services/subghz_radio_broker/subghz_radio_broker.h`.
