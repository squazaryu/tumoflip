# TumoCard OS

TumoCard OS is the first multi-package runtime built on the bounded TumoVM core.
It exposes enabled applets through NFC ISO14443-4A and an app-local USB CCID
interface without restoring the removed API 87 CCID HAL.

## Storage layout

```text
/ext/apps_data/tumocard_os/applets/<id>/manifest.tca
/ext/apps_data/tumocard_os/applets/<id>/program.tvm
/ext/apps_data/tumocard_os/applets/<id>/settings.tcs
/ext/apps_data/tumocard_os/applets/<id>/state.tvs
```

The registry loads at most four applets. IDs use lowercase ASCII letters,
digits, `_`, and `-`. Duplicate IDs or AIDs, malformed manifests/programs,
oversized state/bytecode, unsupported capabilities, and excess packages fail
closed. A corrupt applet is skipped without granting it a transport.

State and enabled settings are stored with temporary and backup files. NFC and
USB maintain independent selected-applet sessions but route into the same
per-applet VM state.

## Reference applets

On the first launch, an empty registry receives two harmless editable applets:

| Applet | AID | Initial state |
| --- | --- | --- |
| Counter Token | `F0 54 43 41 52 44 01` | `COUNT\0\0\0` |
| Notes Token | `F0 54 43 41 52 44 02` | `NOTE:READY...` |

Both use the bounded SELECT, READ BINARY, and UPDATE BINARY routes from TumoVM.
Replacing their declarative files changes behavior without rebuilding the FAP.

## Compile a custom applet

Create a JSON definition with an ASCII name, unique 5-16 byte AID, 1-64 bytes
of initial state, the `state-v1` profile, and only the `nfc.type4` and
`usb.ccid` capabilities. Compile it directly into an SD-ready directory:

```shell
python3 tools/tumoflip/tumocard_compile.py applet.json /path/to/sd/apps_data/tumocard_os/applets
```

The compiler rejects unknown fields, native profiles, extra hardware
capabilities, and all crypto requests. Version 0.1 intentionally provides no
cryptographic applet API.

## Trust boundary

Version 0.1 applets are unsigned development packages, not security-grade
smart-card applications. Loading an applet grants only the fixed `state-v1`
interpreter, its declared NFC/CCID endpoints, and a maximum of 64 persistent
bytes. It does not grant native execution, firmware symbols, arbitrary storage,
radio/GPIO access, or cryptographic key storage. Review package files before
placing them on the SD card.

## Controls

- Left/Right selects an installed applet.
- OK enables or disables the selected applet and resets active selections.
- Back stops NFC and CCID, saves dirty state, and restores the previous USB mode.

## APDU examples

Select Counter Token:

```text
00 A4 04 00 07 F0 54 43 41 52 44 01
```

Read eight bytes:

```text
00 B0 00 00 08
```

Select Notes Token by changing the final AID byte to `02`. An APDU sent before a
successful SELECT returns `6985`; an unknown or disabled AID returns `6A82`.
