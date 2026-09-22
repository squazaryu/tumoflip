## Unreleased — Dev 009-008
- Sub-GHz: load Frequency Analyzer view/worker and Add Manually from checked feature FALs; retain notebook, preset handoff, radio ownership and creation behavior. Stop worker/callbacks before unmapping.
- Loader: expose structured launch diagnostics, distinguish fragmented/insufficient RAM, add stack preflight and bound manifest reads. F7 API advances additively to 88.13.
- Marauder Companion 7.13-tumo: add Recon/list/Protocol Info/SPIFFS status and backup, fix command-specific Back behavior and offer Wardrive POI via long OK in the running console. Hardware acceptance remains pending.

## Tumoflip Dev 009-007
- Specter FW Package 3.2: adapt upstream 3.0.1 survey/fingerprint spacing, suppress the reader ring during calibration, report TOO SHORT for empty surveys under 10 seconds, and wrap text log entries while retaining checked SD writes and rollback.
- Sub-GHz: allow existing PSA/PSA2 receivers through AM as well as FM filtering; add a bounded Honda 315 MHz RX profile shared by Standard/Read RAW and ProtoPirate. Existing profile indices and independent settings remain stable.

## Tumoflip Dev 009-006
- Sub-GHz: keep the profile-owned `TumoHonda` receiver preset out of the generic Core modulation choices. The Honda RX profile still selects it and displays it as `Honda RX Custom`; saved actual preset indices remain stable. The Honda/Acura protocol remains in the Shuka Auto pack.

## Tumoflip Dev 009-005
- Bluetooth Remote 1.3: fix the cold-start `Cannot select peer` failure by using one invalid GAP connection handle; valid handle zero is never confused with disconnection. Show readable temporary device labels, ask for a local name before first connection, and preserve existing names and pairing keys. Update both firmware and FW Packages to receive this fix.
- Bluetooth/USB Remote: stop and drain Mouse Jiggler timers on Stop/Back, reset the running state, use one-shot Stealth scheduling, and wait for BLE connection. Keep Tumoflip device selection and package-only Bluetooth Remote (upstream #1112).
- Sub-GHz: free Security+ 2.0 scratch transmitters, use local ownership in all 15 existing generators, clear the live pointer on TX stop, and log missing encoders. No new generation or transmission functionality (upstream #1104, follow-ups #1159/#1160).
- RFID: show an in-range numeric starting value, use consistent manual-format labels and simplify field restoration without losing location sidecars. Existing HID/Casi formats remain unchanged; S10401/Casi hardware edge cases remain pending (upstream #1149/#1158).
- iButton: select permitted blank types, show the current write target and handle an empty/inapplicable selection. Preserve Full Writing and legacy FAP callback values; unreadable settings disable Write ID targets, and opening settings without edits does not overwrite a file (upstream #1153).
- Local F7 API 88.12 adds five iButton functions without removing existing exports. Install this firmware first, then FW Packages Dev 019 for Bluetooth Remote 1.3. Existing independent package updates remain cumulative. Upstream RW1990.1 issue #1143 is not resolved by these changes.
- Hardware acceptance remains pending. The reverted loader relaunch series, new menu layouts, Community Pack refresh and monitoring changes are excluded. No reset or re-pairing is required by the migration.

## Tumoflip Dev 009-004
- Bluetooth Remote: choose a bonded device before advertising, keep only that identity allowed, and never fall back to the phone when the chosen host is unavailable. Add local device labels, individual unpairing and explicit host-side pairing.
- Keep existing HID keys/config paths. Selection preferences are separately journalled; missing key stores are initialized independently of Companion, while corrupt/oversized stores fail closed.
- Move only `hid_ble.fap` into FW Packages Base. USB Remote stays bundled; BadUSB and the Kodi remote are unchanged. Install Companion 1.11.23 first, this firmware second, and FW Packages Dev 018 last.
- Toyota: add receive-only Variant C alongside A/B. Preserve the variant and trailing bits in saved captures; reject malformed metadata and incomplete/oversized frames. No new encoder or secret-recovery behavior.
- F7 API advances additively to `88.11` for the four Bluetooth peer-selection service calls. Existing public profile layout is unchanged. Hardware acceptance remains pending.

## Tumoflip Dev 009-003
- NFC: compare complete FeliCa/DESFire results and stop completed MIFARE Classic reads without changing the CUID/Skip paths.
- Sub-GHz: add receive-only Monarch/KEY metadata, per-receiver AM/FM filtering, and bounded Manual/seven-profile frequency/modulation choices; Standard and Read RAW retain independent settings.
- ARF: add the package-only Capture Inspector/Compare for saved captures and a bounded Status probe that distinguishes header compatibility from unverified integrity/imports.
- Quac 0.11.0: validate action durations and playlist pauses, preserve millisecond timing, and handle Back/cancellation without releasing RAW worker context early.
- Specter 3.1.0-tumo: package-only passive NFC field diagnostics with checked log writes, sync/close failures, capacity checks and best-effort rollback. The adapted app has explicit protected source ownership.
- API advances additively from `88.9` to `88.10`. Rebuilt paired packages are attached; use the matching FW Packages channel for separately installed applications.
- No factory reset, new protocol encoder or secret-recovery feature. Hardware acceptance remains pending.

## Tumoflip Dev 009-002
- RFID: add manual FC/ID entry and app-side renderers for HID H10302/H10304/H10306, AMAG S10401, Corporate 1000 35-bit and Casi-Rusco C10106; Generic HIDProx rendering now reports frame length without duplicating raw data.
- GUI: an empty bounded NumberInput field now resolves to the nearest valid value around zero when submitted.
- ARF packages: add a file-only Capture converter in the ARF hub. It copies canonical `.psf` ↔ `.sub` captures without rewriting protocol names or dropping metadata. Originals and existing destination files are never overwritten; success requires sync, close and byte-for-byte readback.
- Converter: allow cancellation while scanning or copying; report invalid files, SD errors and cancellation separately. No cryptographic recovery or new protocol encoder is added.
- API remains `88.9`; no user-data migration is required. Hardware acceptance is still pending.

## Tumoflip Dev 009-001
- LFRFID: harden Keri PSK decoding against slicer skew by checking two complete frames for matching IDs before accepting a read; include regression vectors for clean and mismatched frames.
- NFC: award the existing Dolphin deed when launching directly into card emulation, matching the saved-card path.
- LFRFID: choose which writable chips the app and CLI may try (T5577, EM4305, and each Hitag micro variant); all targets remain enabled by default and writes still verify the result.
- LFRFID: add an opt-in `8268` target for writing EM4100 RF/64 frames to ID8268 / Hitag S clone chips; UID confirmation is required before the two data pages are written, and the target stays disabled by default to protect genuine Hitag S application data. The Hitag S implementation is delivered as a lazy `lfrfid_hitags.fal` package, so its reader/writer is loaded only when that target is selected.
- Build: enable resident-image LTO while keeping FAPs on the normal symbol-table pipeline; the F7 C1 image shrinks by about 8.3 KiB and now leaves an 8 KiB physical C2 gap. `LTO=0` remains available for A/B diagnostics.
- LFRFID: keep Hitag Micro and Hitag S BPLM cell timings in one shared header; this is a source-drift guard with no behavioral or binary change.
- Core correctness: align startup-hook signatures with `FlipperInternalOnStartHook` and make Storage Settings copy a concrete descriptor with its full option array, removing LTO diagnostics without changing behavior.
- Core audit: review the resident LFRFID/Sub-GHz symbols and public API exports; no safe dead-code removal was found without cutting Standard/ARF or third-party FAP functionality.
- API: F7 advances from `88.8` to `88.9` for the default write-target accessor; existing API 88 FAPs remain major-compatible.

## Tumoflip Dev 008-027
- GUI: **Expose `view_dispatcher_check_id()`** so FAPs can test view-ID availability without mutating the dispatcher or tripping the duplicate-ID guard. Adapted from official Flipper firmware PR #4422.
- API: F7 advances from `88.6` to `88.7`; the maintained F18 compatibility target advances from `88.0` to `88.1`. Existing FAPs are rebuilt from the same release tree.
- Upstream audit: official Flipper `1.5.0-rc` parity was reviewed; Elplast, Cardin S449, date/time input, canvas buffer, storage boot handling, and the relevant NFC/GUI safeguards are already present in Tumoflip. The incomplete hotel parser was not imported.
- ARF audit: `51d4700d`, `892092a1`, and `51efff55` remain excluded because of Toyota/PSA2 regressions, broad unvalidated protocol rewrites, a committed object file, and an absent phone-side BLE offload implementation.

## Tumoflip v1.0.7 / t-flppr-fw-007
- Current API: 88.4 (F7 stable API for t-flppr-fw-007)
* JS Runner and NFC FAPs can resolve shared soft-float helpers from the F7 firmware, reducing duplicated libgcc code while keeping the shared-library path explicit for compatible FAPs.
* SubGHz: **Fix endless TX causing RAW files to be transmitted and crash the system** (via RPC / Mobile App) (Fixes issue #1008)
* SubGHz: **Add Telcoma/Cardin EDGE protocol** (32bit, Static) (by @half2me | PR #1001)
* LFRFID: **Support of Hitag Micro chips** (8265/8210/H5.5) (by @mishamyte | PR #1002)
* LFRFID: **Wipe T5577** (reset to blank, with read-back verification) (by @mishamyte | PR #1003)
* LFRFID: **Read T5577 tags holding multiple EM4100 IDs again** - a T5577 written with several EM4100 IDs (e.g. via Multiwriter) hung on Read since the Electra protocol was added; also resets a stale PAC/Stanley decoder buffer (by @mishamyte | PR #1025 | Fixes #1024)
* NFC: **Native MIFARE Plus support in SL3** - MIFARE Plus is now a first-class protocol instead of detection-only: read (AES auth + encrypted/plaintext blocks, admin keys & config, originality signature), automatic dictionary attack with a per-UID key cache (instant re-reads of saved cards), full SL3 emulation with shadow-writeback (a reader can authenticate, read & write the recovered card), write/update-to-card, GetVersion + ATS-based S/X/SE/EV1/EV2 detection, "Add Manually" for 18 Plus variants, and MIFARE-Classic-style dump & keys screens; SL0/SL1/SL2 stay untouched (by @mishamyte | PR #1032 | Closes #1031)
* NFC: Show MIFARE Ultralight/NTAG PWD & PACK in full info view / on read screen too (by @mishamyte | PR #1010 #1011)
* NFC: **Add Bambu Lab filament spool parser** (type, color, code, temps, spool specs) (ported from [uzyn/flipper-bambu](https://github.com/uzyn/flipper-bambu), GPL-3.0)
* NFC: **Fix MIFARE Plus 2K SL1 transit parsers** - Troika, Plantain, SevPPK, SZPPK and Two Cities stopped parsing once Plus 2K SL1 cards began reporting as the new `2K` type (PR #1016): the parsers only knew `1K`/`4K` and required a full-card read. They now treat 2K as the 1K these cards present and accept a partial read (by @mishamyte | PR #1038 | Fixes #1037)
* NFC: **Align MIFARE type detection with NXP AN10833** - Classic/Ultralight/NTAG/Plus sizing & security level; fixes Mifare Mini clone mis-detection and Ultralight AES read hang (by @mishamyte | PR #1014)
* NFC: **Read MIFARE Plus 2K in SL1 as full 32 sectors / 64 keys** - a Plus 2K in SL1 is byte-identical to a Classic 1K in SAK/ATQA and was mis-sized as 1K; it is now told apart by its ISO14443-4 ATS (Plus S/X signature), while Plus SE, SmartMX, magic "Perfect CUID" and plain 1K stay 1K (by @mishamyte | PR #1016)
* NFC: **Show a loading screen while a large CUID dictionary loads on Read** - animated spinner + "CUID dictionary is loading" label instead of a blank/frozen-looking screen while a per-UID (MFKey-recovered) dictionary is scanned (by @mishamyte | PR #1022)
* Apps: **NFC Magic** - Gen2 CUID/static-nonce detection, Gen1 4b/7b UID, length-aware wipe & write guard (by @mishamyte)
* Apps: Build tag (**10jun2026**) - **Check out more Apps updates and fixes by following** [this link](https://github.com/xMasterX/all-the-plugins/commits/dev)
## Other changes
* Power: report the actual fuel-gauge and charger initialization result instead of logging `Init OK` after a failed boot attempt (adapted from Unleashed #1132).
* Plugin loader: continue scanning after an invalid or incompatible `.fal`, return the first real load/read error, and keep the Sub-GHz radio driver visible when another plugin fails (adapted from Unleashed #1133; existing filename-prefix filtering is preserved).
* GUI: Add an app-owned startup loading view for Archive and Desktop settings; the looping indicator is stopped and reset on the first real view, and queued startup input is discarded. The direct FAP loader overlay remains disabled.
* NFC: Save recovered MIFARE Classic keys into the user dictionary with duplicate filtering, read-only scans, a verified backup, synchronized append, and rollback on write failure (adapted from Unleashed 95c35fb).
* Infrared: Save the currently selected Universal Remote candidate as a new uniquely named remote or append it to an existing remote; existing files are backed up before append and restored on write failure (adapted from Unleashed b9f5789)
* Desktop: Add a second page to the Up-button menu for screen brightness, volume, and vibration; Left/Right switches pages without changing the selected Desktop layout.
* NFC: **Remove unreachable EMV render helpers and stale NFC/backdoor exports**, eliminating an undefined plugin symbol and reducing link-time surface (Unleashed PR #1085)
* NFC: **Parser declines and unknown ticket layouts are now logged at debug level**, while MIFARE Classic parser guidance explicitly requires checking block data (Unleashed PR #1097)
* NFC: **Ultralight read results now distinguish failed authentication from an intentionally skipped attempt**, and never display masked zero bytes as a captured password (Unleashed PR #1090)
* NFC: **Never probe a generated Ultralight password when the UID is unsuitable, or when AUTHLIM is unreadable**; explicit skips preserve the card's remaining authentication attempts (Unleashed PRs #1086/#1089)
* Apps: Update FindMy app
* Fix BLE sync, fix possible delay related issues
* Disabled debug and trace logs in the FW binary (apps .fap's are not affected) to free up some flash space for new features
* NFC: Fix typo in SLIX poller (by @WillyJL)
* NFC: Internal MIFARE Plus cleanup - data-drive the "Add Manually" generator variants and unify the admin-key address mapping into one source of truth; small internal-flash saving, no functional change (by @mishamyte | PR #1035)
* NFC: Preserve the reviewed protocol-scene plugin split and API-v3 dispatch contract, keeping protocol-only scenes and transit parsers out of the resident NFC app (Tumoflip #346 / Unleashed #1073)
* GUI: FileBrowser entries retain only their names and reconstruct paths on demand, reducing RAM use in large directories (Momentum #362 / upstream b8757a5e7a)
* Archive: Keep the cursor valid when a directory has more than 220 entries (Momentum #362 / upstream 757cca0279b)
* Sub-GHz: Guard transmitter cleanup when no transmitter is available (Unleashed #1104 / Tumoflip #394)
* Sub-GHz: Reject BinRAW encoder writes that would exceed the upload buffer (Unleashed #1105 / Tumoflip #394)
* NFC: Reject FeliCa Lite dumps with invalid block counts (Unleashed #1106 / Tumoflip #394)
* Toolbox: Compare the complete storage of each SimpleArray element (Unleashed #1107 / Tumoflip #394)
* F7 serial: Reject the invalid expansion serial sentinel (Unleashed #1108 / Tumoflip #394)
* Release validation: model the updater's page-aligned C1 erase range so the final C1 page can end at the C2 boundary without weakening DfuSe address checks
* Sub-GHz & Storage: Share duplicated protocol allocation, deserialization, serialization, and command-dispatch bodies, freeing about 4.8 KB of internal flash without changing the exported SDK API (Unleashed PR #1116)
* Sub-GHz: Adapt upstream `e6ded9b` (plus follow-up `3431d19`) without replacing Tumoflip's custom protocol set: Nice O-Code installer-code tooling, Security+ 2.0 86-bit keypad/PIN support, standalone 42-bit Prastel rolling code, and KeeLoq JCM Gen2/Stagnoli/Telcoma learning variants.
* Sub-GHz: Add `nice_o_code` and `secplus_pin` system FAPs, register Prastel in the shared Standard/ARF protocol registry, and expose the new helpers through API 88.6.
* Sub-GHz: Keep the existing Tumoflip KeeLoq keystore intact and load the upstream encrypted additions as a separate read-only resource; user keys and custom manufacturer entries remain writable and unchanged.
* Apps: Add Nearby Files as a Base FW Package. It sorts `.sub`, `.nfc`, `.rfid`, and `.ibtn` captures by distance using a one-shot TumoCompanion GPS location over BLE, with the existing Flipper GPS/NMEA source retained as a fallback. The Community Pack copy is excluded to prevent duplicate ownership.
<br><br>

----

[-> How to install firmware](https://github.com/DarkFlippers/unleashed-firmware/blob/dev/documentation/HowToInstall.md)

[-> Unleashed FW Web Installer](https://web.unleashedflip.com)

## Please support development of the project

| Service                                                                                                                                                                                        | Remark                    | QR Code                                                                                                                                                                                                                             | Link/Wallet                                                                                       |
|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------|
| <img src="https://cdn.simpleicons.org/patreon/dark/white" alt="Patreon" width="14"/> **Patreon**                                                                                               |                           | <div align="center"><a href="https://github.com/user-attachments/assets/a88a90a5-28c3-40b4-864a-0c0b79494a42"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | [patreon.com/mmxdev](https://patreon.com/mmxdev)                                                  |
| <img src="https://cdn.simpleicons.org/boosty" alt="Boosty" width="14"/> **Boosty**                                                                                                             | patreon alternative       | <div align="center"><a href="https://github.com/user-attachments/assets/893c0760-f738-42c1-acaa-916019a7bdf8"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | [boosty.to/mmxdev](https://boosty.to/mmxdev)                                                      |
| <img src="https://gist.githubusercontent.com/m-xim/255a3ef36c886dec144a58864608084c/raw/71da807b4abbd1582e511c9ea30fad27f78d642a/cloudtips_icon.svg" alt="Cloudtips" width="14"/> CloudTips    | only RU payments accepted | <div align="center"><a href="https://github.com/user-attachments/assets/5de31d6a-ef24-4d30-bd8e-c06af815332a"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | [pay.cloudtips.ru/p/7b3e9d65](https://pay.cloudtips.ru/p/7b3e9d65)                                |
| <img src="https://raw.githubusercontent.com/gist/PonomareVlad/55c8708f11702b4df629ae61129a9895/raw/1657350724dab66f2ad68ea034c480a2df2a1dfd/YooMoney.svg" alt="YooMoney" width="14"/> YooMoney | only RU payments accepted | <div align="center"><a href="https://github.com/user-attachments/assets/33454f79-074b-4349-b453-f94fdadc3c68"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | [yoomoney.ru/fundraise/XA49mgQLPA0.221209](https://yoomoney.ru/fundraise/XA49mgQLPA0.221209)      |
| <img src="https://cdn.simpleicons.org/tether" alt="USDT" width="14"/> USDT                                                                                                                     | TRC20                     | <div align="center"><a href="https://github.com/user-attachments/assets/0500498d-18ed-412d-a1a4-8a66d0b6f057"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `TSXcitMSnWXUFqiUfEXrTVpVewXy2cYhrs`                                                              |
| <img src="https://cdn.simpleicons.org/ethereum" alt="ETH" width="14"/> ETH                                                                                                                     | BSC/ERC20-Tokens          | <div align="center"><a href="https://github.com/user-attachments/assets/0f323e98-c524-4f41-abb2-f4f1cec83ab6"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `0xFebF1bBc8229418FF2408C07AF6Afa49152fEc6a`                                                      |
| <img src="https://cdn.simpleicons.org/bitcoin" alt="BTC" width="14"/> BTC                                                                                                                      |                           | <div align="center"><a href="https://github.com/user-attachments/assets/5a904d45-947e-4b92-9f0f-7fbaaa7b37f8"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `bc1q0np836jk9jwr4dd7p6qv66d04vamtqkxrecck9`                                                      |
| <img src="https://cdn.simpleicons.org/solana" alt="SOL" width="13"/> SOL                                                                                                                       | Solana/Tokens             | <div align="center"><a href="https://github.com/user-attachments/assets/ab33c5e0-dd59-497b-9c91-ceb89c36b34d"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `DSgwouAEgu8iP5yr7EHHDqMNYWZxAqXWsTEeqCAXGLj8`                                                    |
| <img src="https://cdn.simpleicons.org/dogecoin" alt="DOGE" width="14"/> DOGE                                                                                                                   |                           | <div align="center"><a href="https://github.com/user-attachments/assets/2937edd0-5c85-4465-a444-14d4edb481c0"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `D6R6gYgBn5LwTNmPyvAQR6bZ9EtGgFCpvv`                                                              |
| <img src="https://cdn.simpleicons.org/litecoin" alt="LTC" width="14"/> LTC                                                                                                                     |                           | <div align="center"><a href="https://github.com/user-attachments/assets/441985fe-f028-4400-83c1-c215760c1e74"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `ltc1q3ex4ejkl0xpx3znwrmth4lyuadr5qgv8tmq8z9`                                                     |
| <img src="https://bitcoincash.org/img/green/bitcoin-cash-circle.svg" alt="BCH" width="14"/> BCH                                                                                                |                           | <div align="center"><a href="https://github.com/user-attachments/assets/7f365976-19a3-4777-b17e-4bfba5f69eff"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `qquxfyzntuqufy2dx0hrfr4sndp0tucvky4sw8qyu3`                                                      |
| <img src="https://cdn.simpleicons.org/monero" alt="XMR" width="14"/> XMR                                                                                                                       | Monero                    | <div align="center"><a href="https://github.com/user-attachments/assets/96186c06-61e7-4b4d-b716-6eaf1779bfd8"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `41xUz92suUu1u5Mu4qkrcs52gtfpu9rnZRdBpCJ244KRHf6xXSvVFevdf2cnjS7RAeYr5hn9MsEfxKoFDRSctFjG5fv1Mhn` |
| <img src="https://cdn.simpleicons.org/ton" alt="TON" width="14"/> TON                                                                                                                          |                           | <div align="center"><a href="https://github.com/user-attachments/assets/92a57e57-7462-42b7-a342-6f22c6e600c1"><img src="https://github.com/user-attachments/assets/da3a864d-d1c7-42cc-8a86-6fcaf26663ec" alt="QR image"/></a></div> | `UQCOqcnYkvzOZUV_9bPE_8oTbOrOF03MnF-VcJyjisTZmsxa`                                                |


#### Thanks to our sponsors who supported project in the past and special thanks to sponsors who supports us on regular basis:
@mishamyte, ClaraCrazy, Pathfinder [Count Zero cDc], callmezimbra, Quen0n, MERRON, grvpvl (lvpvrg), art_col, ThurstonWaffles, Moneron, UterGrooll, LUCFER, Northpirate, zloepuzo, T.Rat, Alexey B., ionelife, ...
and all other great people who supported our project and me (xMasterX), thanks to you all!


## **Recommended update option - Web Updater**

### What `e`, ` `, `c` means? What I need to download if I don't want to use Web updater?
What build I should download and what this name means - `flipper-z-f7-update-(version)(e / c).tgz` ? <br>
`flipper-z` = for Flipper Zero device<br>
`f7` = Hardware version - same for all flipper zero devices<br>
`update` = Update package, contains updater, all assets (plugins, IR libs, etc.), and firmware itself<br>
`(version)` = Firmware version<br>
| Designation | [Base Apps](https://github.com/xMasterX/all-the-plugins#default-pack) | [Extra Apps](https://github.com/xMasterX/all-the-plugins#extra-pack) |
|-----|:---:|:---:|
| ` ` | ✅ |  |
| `c` |  |  |
| `e` | ✅ | ✅ |

**To enable RGB Backlight support go into LCD & Notifications settings**

⚠️RGB backlight [hardware mod](https://github.com/quen0n/flipperzero-firmware-rgb#readme), works only on modded flippers! do not enable on non modded device!


Firmware Self-update package (update from microSD) - `flipper-z-f7-update-(version).tgz` for mobile app / qFlipper / web<br>
Archive of `scripts` folder (contains scripts for FW/plugins development) - `flipper-z-any-scripts-(version).tgz`<br>
SDK files for plugins development and uFBT - `flipper-z-f7-sdk-(version).zip`
