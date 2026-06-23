# Sub-GHz Protocol Packs

Tumoflip can load selected Sub-GHz protocols from SD as `.fal` plugins. This
keeps optional protocol code out of the internal firmware image while exposing
the protocols through the normal graphical Sub-GHz application.
This is the supported extension path for Sub-GHz; see
[Sub-GHz Architecture](subghz-architecture.md) for the firmware/SD boundary.

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

Protocol Packs currently extend the graphical system Sub-GHz application. The
Sub-GHz CLI and external applications such as ProtoPirate keep their own
registries and do not automatically inherit these packs.

## Runtime groups

Flipper RAM cannot safely map all 24 packs and allocate every decoder at the
same time. The Receiver configuration therefore exposes one active group:

- `Core`: no external packs;
- `Legacy`: VAG, Kia v0-v2, and Mitsubishi v0;
- `Kia`: all Kia packs;
- `Ford`: Ford v0-v3;
- `Europe`: Fiat Marelli, Land Rover, Porsche, PSA, and VAG;
- `Asia/US`: Chrysler, Mazda, Mitsubishi, and Subaru;
- `Alarm`: Scher-Khan, Sheriff CFM, and StarLine.

`Legacy` is the default and preserves the pack set shipped before v0.2.0. A
group change is applied immediately: the application stops RX, replaces the
plugin registry and decoder instances, restores callbacks and filters, then
resumes RX. The selection is also stored in the existing Sub-GHz settings.
Every pack remains on SD; the selection only controls which files are mapped
into RAM for the current session.

## Protocol Pack Inspector

Open `Sub-GHz` -> `Read` -> receiver configuration -> `Pack Status`, then
press `OK`.
The summary shows:

- active group;
- loaded versus expected external plugins;
- total registered protocols and the built-in protocol count;
- firmware API and Protocol Pack API versions;
- approximate RAM occupied by the active registry and mapped plugins.

The list below the summary reports every expected `.fal` and its loaded
protocol name. Failures distinguish missing or invalid files, insufficient
RAM, manifest and hardware-target problems, old/new firmware API, missing
imports, application or Protocol Pack API mismatch, invalid protocol entries,
and duplicate protocol names.

The report is captured by the normal loader. Opening Inspector does not scan
the SD card again or load a second copy of any plugin.

## Current packs

The 24 packs cover every protocol enabled by the ARF registry that is not
already built into tumoflip:

- Chrysler
- Fiat Marelli
- Ford v0, v1, v2, and v3
- Kia v0, v1, v2, v3/v4, v5, v6, and v7
- Land Rover v0
- Mazda Siemens and Mazda v0
- Mitsubishi v0
- Porsche Cayenne
- PSA
- Scher-Khan
- Sheriff CFM
- StarLine
- Subaru
- VAG

Fiat SPA, Suzuki, and Toyota remain in the built-in registry because no
isolated ProtoPirate replacement exists for them. BMW CAS4 and Honda remain
disabled, matching the upstream ARF registry.
