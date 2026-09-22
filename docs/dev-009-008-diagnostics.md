# Dev 009-008: feature residency and launch diagnostics

## Sub-GHz feature modules

Frequency Analyzer's view and worker now load from
`apps_data/subghz/plugins/subghz_frequency_analyzer.fal`. The host retains its
scene and receiver transitions, observation notebook, preset selection and radio
broker ownership. Switching away joins the worker before removing the view and
unmapping code. Add Manually's six scene bodies and generators load from
`subghz_add_manually.fal`; their algorithms and saved formats are unchanged.
Unloading waits until nested plugin callbacks have returned. The host clears
borrowed ByteInput strings and callbacks before unmapping.

This is a selective adaptation of Unleashed #1131/#1134; source exclusions from
#1135 were already present. The Tumoflip private feature ABI is 1001, deliberately
distinct from upstream. Bump it when shared structs/enums/callback signatures
change. Public firmware exports and the private Sub-GHz table are checked
separately against both FAL import lists and the resident ELF.

Measured mapped code/data sections: Analyzer 7296 B, Add Manually 11509 B.
Loader, view and worker allocations are additional while the feature is open.
The local updater has a 16384-byte physical C2 gap versus 4096 in 009-007;
physical peak-RAM and repeated-entry acceptance remain pending.

Both FALs are mandatory updater resources, with archive hashes checked by the
release validator. Missing/outdated modules show a recoverable error. These
features now require SD resources at launch; their menu routes remain the same.
The separate ARF analyzer stays bundled with ARF. Its shared view/worker bodies
remain compared by the drift checker, normalizing only include paths.

## Loader diagnostics (F7 API 88.13)

Four additive exports: `loader_start_with_diagnostics`,
`loader_get_last_diagnostic`, `flipper_application_get_memory_failure`,
`plugin_manager_load_single_detailed`. Existing loader enum values and
entry points are preserved.

`loader diag` returns the last attempt. App Bridge runtime advertises `ld=1`
and accepts `loader_diag`. Atomic launch callers can request a diagnostic with
the start result through the loader queue. The schema-1 record contains sequence,
code, sanitized basename, required/firmware API, target and allocation evidence.
No arguments, capture contents or keys are included. Records are RAM-only and
reset on reboot; this does not implement the deferred crash journal #409.

Cases include busy, absent SD/file, invalid ELF/manifest, missing imports,
incompatible API/target, non-runnable plugin, relocation failure and insufficient
memory. ELF section allocation evidence is sampled before partial-load cleanup:
enough total heap but no sufficiently large block reports fragmented_memory.
The required block includes the existing 1024-byte conservative reserve.
Manifest/API fields can be zero when metadata was not yet available.
The app stack also gets a conservative memory preflight; this is not a guarantee
against concurrent allocations or the running application's own allocations.

Malformed oversized metadata cannot write beyond the supported manifest struct.
Unknown metadata suffix bytes are ignored; the existing manifest-version check
still determines compatibility.

## Marauder Companion 7.13-tumo

Selectively adapted author b39a2cb6c8ec13670ee0268fb803f747a1757482:
Recon Wi-Fi/BLE/status/stop, additional list queries, Protocol Info, SPIFFS
status/backup, NMEA stop hint and removal of the dead GPS Tracker menu entry.
Existing GPS Data modes, user files, module routing and existing actions remain.
No SPIFFS restore or new Fox Hunt/attack option is added.

Back from a Recon scan uses `recon stop`; diagnostic/status/backup pages do not
stop an unrelated scan on exit. Correction to the initial investigation:
ESP32Marauder 1.17.0 also stops Recon in its generic stopscan handler.

Wardrive POI is requested by holding OK in the live Wardrive console. A standalone
menu entry would be ineffective because Back stops Wardrive; the transparent
input layer therefore preserves the running session and ordinary scrolling/Back.
The ESP firmware decides whether an active GPS session can accept the marker;
the Flipper never displays a fabricated success result.

Board command semantics were checked against ESP32Marauder
8ae4622abcc9c9c5729d4e97491907581d7f0c34. Features remain conditional on the
connected board's firmware/hardware. This change does not flash the ESP32.
Marauder is delivered by the firmware's resource archive. The existing independent
FW Packages Dev 020 catalog remains compatible; no mass package rebuild is needed
for these additive API exports.

## Acceptance

Host tests exercise plugin unload ordering/nested callbacks, index and menu bounds,
Recon cleanup, Wardrive input propagation, loader classification/redaction and
oversized metadata. Import checks cover both feature FALs against their actual host.
Full updater validation checks their presence and hashes.

Physical acceptance still required:

1. Repeated Frequency Analyzer enter/Back and long-OK/preset handoff; internal and
   external radio, notebook save, no stale loading screen or heap drift.
2. Complete Add Manually/create/save/reopen for representative existing formats,
   Back at each step and missing/incompatible feature FAL error paths.
3. Loader errors through UI, CLI, shortcuts and RPC; compare diagnostic codes,
   SD absence, corrupt files, API/target mismatch and memory pressure.
4. Marauder C5/Module One command availability, Recon start/Back, NMEA stop,
   list/status queries, disconnect/reconnect and Wardrive long-OK marker.
5. Tumo Acceptance smoke/report on the final installed firmware.
