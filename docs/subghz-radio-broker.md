# Sub-GHz Radio Broker

The Radio Broker is a system service that owns arbitration state for Sub-GHz
radio clients. It provides:

- an exclusive lease with an owner name;
- external CC1101 power ownership;
- preservation of OTG power enabled by another subsystem;
- selected internal/external device status;
- automatic external-power cleanup when a valid lease is released.

The system Sub-GHz application and all current ARF radio applications are
Broker clients. They acquire a lease
before initializing the device registry and releases it after stopping the
radio and deinitializing the registry. External-module probing and fallback to
the internal CC1101 keep their existing behavior.

RollJam reports a dual-radio selection and routes its external CC1101 power
through the Broker while retaining its direct internal-RX/external-TX logic.
ProtoPirate and Sub-GHz Bruteforcer keep their custom device loaders, but pass
the active lease explicitly so probing, fallback, and cleanup preserve Broker
power ownership.

Applying a global lease inside
`subghz_devices_init` would turn an existing missing `deinit` into a persistent
radio lock, so migration remains explicit per application.

The public API is in
`applications/services/subghz_radio_broker/subghz_radio_broker.h`.
