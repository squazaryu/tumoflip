# Unleashed 1428–1435: selective Tumoflip integration

Base: published `t-dev-009-004`, `4a61c5f6cb05c225996410b17690e2bb26de58d4`.
Scope approved by the owner: HID timers, Sub-GHz cleanup, final RFID review,
and iButton blank selection. No release, device installation, Community Pack
acceptance, protected-audit decision or automation update is implied.

## Source and adaptation boundaries

| Area | Upstream | Tumoflip treatment |
| --- | --- | --- |
| Mouse Jiggler | [8fdb70dd32 / #1112](https://github.com/DarkFlippers/unleashed-firmware/pull/1112) | Both transports; clear state, stop/drain callbacks outside the model lock, one-shot Stealth, connection-aware BLE, nonzero bounded movement |
| Sub-GHz | [7f372c908a / #1104](https://github.com/DarkFlippers/unleashed-firmware/pull/1104), [#1159](https://github.com/DarkFlippers/unleashed-firmware/issues/1159), [#1160](https://github.com/DarkFlippers/unleashed-firmware/issues/1160) | Keep existing NULL-safe free and calloc initialization; clear live TX after stop; all 15 generation helpers own temporary transmitters locally; close the Security+ 2.0 leak |
| RFID | [9effa4b44f / #1149](https://github.com/DarkFlippers/unleashed-firmware/pull/1149) | Existing formats preserved; valid visible NumberInput initial value, consistent labels and simpler field restoration |
| iButton | [c74f6b1d57 / #1153](https://github.com/DarkFlippers/unleashed-firmware/pull/1153) | Blank selection, current-target display, CLI agreement, no-target terminal state and smaller interrupt-masked write windows |

Bluetooth Remote remains package-only at `apps/Bluetooth/hid_ble.fap`. The 009-004
device picker, selected-peer policy, bonding journal and Companion restoration are
unchanged. USB Remote remains bundled. BadUSB and Kodi Remote are unchanged.
Destruction also clears/drains timers when an ordinary view-exit callback did not run.

Sub-GHz changes do not alter key creation, protocol encoding or transmission capabilities.
Radio broker/diversity and Standard/Read RAW settings are retained. The separate
missing-preset lookup question (#1161) was reviewed but is not included: changing its
contract needs coordinated checks of all callers, including the SubGHz Remote submodule.

RFID retains location-sidecar requests. These are final follow-ups to formats already
shipped in 009-002, not new formats. Round-trip fixtures do not establish the still-pending
S10401 facility-code width or Casi-Rusco offset boundary; see
[upstream #1158](https://github.com/DarkFlippers/unleashed-firmware/issues/1158).

## iButton compatibility and storage

- Four existing writers: RW1990.1, RW1990.2, TM2004, TM01x. No new writer protocol.
- Settings are stored at `/ext/ibutton/.ibutton.settings`; no reset/migration of key files.
- A genuinely missing settings file uses the historical defaults. An unreadable,
  unsupported or corrupt file disables Write ID targets rather than re-enabling everything.
- Reading settings never rewrites them. Merely visiting the settings page does not save;
  an explicit edit opts into replacement, and save failure is surfaced.
- Full Writing is independent of the blank mask. An enabled but inapplicable mask is a
  terminal error, not an endless spinner.
- Legacy FAP worker clients retain the old callback result set. New progress/results are
  opt-in via `ibutton_worker_set_write_targets()` on an idle worker.
- Settings-page callbacks are detached before unloading its embedded FAL.
- Upstream [RW1990.1 #1143](https://github.com/DarkFlippers/unleashed-firmware/issues/1143)
  is still unresolved; this feature is not a fix for that protocol issue.

## API and delivery

Local F7 API changes additively from 88.11 to 88.12, adding only:

- `ibutton_settings_get_write_targets`
- `ibutton_settings_set_write_targets`
- `ibutton_worker_get_write_chip_name`
- `ibutton_worker_set_write_targets`
- `ibutton_write_target_name`

Existing exports/signatures are unchanged. Local API numbering is not upstream parity.
The new `ibutton_settings.fal` is embedded in the bundled `ibutton.fap`; matching SD
resources are mandatory. Rebuild FW Packages, especially Bluetooth Remote, for the next
publication. Current Dev 009-004, FW Packages Dev 018 and stable assets are immutable and
were not replaced. Local validation archives retaining the old version suffix are **not**
release candidates for installation; publication needs a new version and exact clean rebuild.

## Validation and acceptance

Local validation on 2026-09-21: full host suite 971/971; 12 focused instrumented
tests pass ASan/UBSan, including four HID transport/view combinations. LLVM measured
198/209 (94.74%) regions in the explicitly selected production functions, excluding
mocked macro expansions and transport implementations. No whole-firmware coverage claim.

Firmware/updater/SDK and managed applications build with toolchain 39 and `-j2`.
API comparison reports five additions, zero removals/signature changes. Recursive ELF
import audit passes 6/6 artifacts: four changed FAPs and two embedded settings FALs.
The paired package archive contains 117 entries (ARF 14, base 29, Module One 43, protocol
packs 31). Core C1 end changes from `0x080D5238` to `0x080D5638` (+1024 B); section gap
is 6600 B versus 7624 B, physical erase-aligned gap stays 4096 B. Updater is 119097 B.
Existing duplicate-environment warnings in the TumoVM/CCID build graph remain; no
compiler error or unresolved import was observed. Hardware acceptance remains pending.

Automated checks cover both HID transports, both jiggler modes, Start/Stop/Back/re-entry,
destruction without exit, interval bounds, disconnected BLE, signed movement bounds,
repeated/failed scratch allocation, numeric input ranges/confirmation, iButton masks,
legacy events, storage failure and Full Writing independence.

`test_upstream_1435_native` and `test_ibutton_write_targets_native` execute extracted
production C under ASan/UBSan. `measure_upstream_1435_tests` additionally measures LLVM
region coverage for an explicit set of production functions; this is **not** coverage
of the entire firmware or proof of RTOS/radio/hardware timing.

Before publication:

1. Run the full host suite and both PR/release regression gates.
2. Build firmware, updater, SDK and the managed FAP set with pinned toolchain and `-j2`.
3. Validate C1/C2 erase boundaries and updater limit, then create the paired package archive.
4. Check imports of `hid_ble`, `hid_usb`, `ibutton`, `lfrfid`, including their embedded FALs.
5. Bump the next Dev version only in the publication workflow; publish paired firmware and
   FW Packages in dependency order. Do not refresh unrelated audit pins or Community Packs.

Physical acceptance is pending:

- Both jigglers over USB/BLE: Start/Stop, Back and reopen, rapid toggles, disconnect/reconnect.
  No stale Stop state, no activity after exit; selected Bluetooth device must not change.
- Return from Bluetooth Remote to Companion with pairing data preserved.
- RFID numeric ranges including zero-excluding fields; Back/Save/reopen and location sidecars.
- iButton settings persist across reboot; write/read-back using owned writable blanks,
  disabled/all-disabled/inapplicable masks, Full Writing and CLI; missing/corrupt settings FAL.
- Existing Sub-GHz saved-file/RPC flow, missing/receive-only encoder error, repeated manual
  creation on owned test data, and ordinary NFC/RFID/SD/FAP smoke tests.

Excluded: reverted loader relaunch series, new desktop layouts, upstream milestone CI,
Community Apps 21sep2026 adoption, new brute-force/recovery behavior and monitoring changes.
