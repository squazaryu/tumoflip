# Sub-GHz Radio Broker

The Radio Broker is a system service that owns arbitration state for Sub-GHz
radio clients. It provides:

- an exclusive lease with an owner name;
- external CC1101 power ownership;
- preservation of OTG power enabled by another subsystem;
- selected internal/external device status;
- observable lifecycle status, lease ticks, and last error;
- automatic external-power cleanup when a valid lease is released.

The system Sub-GHz application and all current ARF radio applications are
Broker clients. They acquire a lease
before initializing the device registry and releases it after stopping the
radio and deinitializing the registry. Core and ARF `subghz_txrx` now report
explicit lifecycle transitions: `probing`, `initialized`, `rx`, `tx`,
`async_rx`, `async_tx`, and `cleaning_up`. External-module probing and fallback
to the internal CC1101 keep their existing behavior.

RollJam reports a dual-radio selection and routes its external CC1101 power
through the Broker while retaining its direct internal-RX/external-TX logic.
ProtoPirate and Sub-GHz Bruteforcer keep their custom device loaders, but pass
the active lease explicitly so probing, fallback, and cleanup preserve Broker
power ownership.

Applying a global lease inside
`subghz_devices_init` would turn an existing missing `deinit` into a persistent
radio lock, so migration remains explicit per application.

Runtime `radio_status` keeps the original `busy`, `external_power`, `device`,
and `owner` fields, then appends `state`, `acquired_tick`, `held_ticks`,
`last_transition_tick`, and `last_error`. This is an observability contract
only; it does not acquire, release, or reconfigure the radio.

The public API is in
`applications/services/subghz_radio_broker/subghz_radio_broker.h`.
