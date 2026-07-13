# TumoFabric Node

`TumoFabric Node` is the on-device UI for the bounded Fabric Counter reference
package from issue #69.

- State is authoritative in the always-running Tumoflip runtime service.
- A local session can run without an iPhone and can later be adopted by the paired
  Companion without resetting the counter.
- Flipper and iPhone can both change the same authoritative counter. Local changes
  do not consume the Companion's idempotency sequence.
- A remote iPhone session survives BLE reconnect while the Flipper stays powered,
  and the Companion polls the node while its Fabric screen is open.
- State is RAM-only in v1 and is intentionally reset by reboot or explicit cancel.
- Only fixed increment/decrement operations are supported. There is no command
  interpreter or remote shell.

Install path:

`/ext/apps/Module One/Labs/tumofabric_node.fap`
