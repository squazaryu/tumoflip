# Sub-GHz Protocol Packs

Tumoflip can load selected Sub-GHz protocols from SD as `.fal` plugins. This
keeps optional protocol code out of the internal firmware image while exposing
the protocols through the normal graphical Sub-GHz application.

## Location

Protocol Packs are installed to:

```text
/ext/apps_data/subghz/plugins
```

Files must start with `protocol_`. Radio-device plugins share the directory but
use the `radio_device_` prefix, so each loader only opens its own plugin type.

## ABI

Protocol Packs use:

- application ID: `subghz_protocol`
- API version: `1`
- entry point: a pointer to one `SubGhzProtocol`

Include `lib/subghz/protocols/plugin.h` and declare the entry point with
`SUBGHZ_PROTOCOL_PLUGIN`. A pack must compile the canonical protocol source
from `lib/subghz/protocols`; copied protocol implementations are not accepted.

The loader rejects invalid entries and ignores a plugin when its protocol name
already exists in the built-in registry. Normal firmware API compatibility
checks still apply, so packs must be rebuilt when their imported firmware API
is no longer compatible.

## Scope

The merged registry is owned by the graphical Sub-GHz application and remains
valid until that application exits. Receivers are destroyed before plugins are
unloaded.

Protocol Packs currently extend only the graphical Sub-GHz application. The
Sub-GHz CLI and external applications such as ProtoPirate keep their own
registries and do not automatically inherit these packs.

## Current packs

- VAG
- Kia v0
- Kia v1
- Kia v2
- Mitsubishi v0
