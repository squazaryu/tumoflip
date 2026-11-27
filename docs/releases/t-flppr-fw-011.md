# Tumoflip Stable 011 — v1.0.11

Promotes the working Dev011-006 baseline plus the owner-tested local 007 UI2
panels. No new protocol, service or optional feature was added during promotion.
Rejected005 is not the source baseline: its recovery service, RTC-journal changes
and expanded resident-library LTO policy are not imported.

## New since Stable010

- Optional native **Panel** desktop, with Classic as the fresh-install default.
  Three pages show SD/Bluetooth-enabled/free-RAM status, idle/sleep information
  and the existing retained fault record. Larger cards use the previous footer
  area; headings are lowered and on-screen button hints are removed.
- Keep the current Panel page behind the unchanged lock/PIN overlay. Short
  Left/Right changes pages and held Left/Right/OK launches assigned apps only
  after unlock. The first page key after backlight sleep stays a Panel action.
  Classic and Panel have independent, verified private shortcut profiles.
- GUI redraw publishes callback/context pairs atomically. Deterministic native
  tests cover attach/detach preemption and the bounded page timer lifecycle.
- Retain the Dev011 line's bounded receive/input checks, RAW Edit and filename
  UI improvements, Weather Editor handling of unknown packet types, BLE bond
  persistence and callback lifetime fixes, and Storage CLI disconnect cleanup.
- No functionality or existing app inventory was removed to make room. Compact
  diagnostic wording retains the full4096-byte physical C2 reserve; ordinary
  core/library compiler and startup policy stays on the working006 profile.

## Compatibility and installation

Public API remains **88.23/F7**. The private Sub-GHz feature contract is **1005**,
not Stable010's1004. Use the resources and compatible FW Packages for this line.

1. Back up `/int`, important SD captures/settings and application data.
2. Install **Firmware → Stable → t-flppr-fw-011** with its native resources.
3. Reconnect, then use **FW Packages → Stable008** after catalog activation.
   It keeps the full143 native files plus two unchanged publisher-owned data
   files inherited from Stable007, not a one-file ZIP. The app compares checksums;
   already matching files do not require retransmission.
4. Current TumoCompanion1.12.0 supports the same API and catalog model; no new IPA,
   Bluetooth reset/re-pair, SD cleanup, factory reset or external-board flash is
   required. Existing captures, app data and paths are not moved or deleted.

Enable panels through **Settings → Desktop → Desktop view → Panel**. Short OK
still opens Apps when unlocked. **Shortcut set** selects the editable profile;
**Panel motion** controls the brief page transition. Existing preferences survive.
RAM is sampled about twice per second while the panel is visible and lit, in
whole KiB; collection pauses behind an app or with the backlight off. BT On is
not a verified phone/RPC health claim, and No record does not mean no prior crash.

## Validation boundary and rollback

The owner installed local007 UI2 and reported apparently stable operation.
This is not exhaustive RF, long-idle, battery or all-device acceptance. The
confirmed GUI race is not asserted to be the sole historic005 reboot cause.

Publication requires full Linux host discovery, normal/diagnostic F7 checks,
CodeQL, exact Stable identity before tagging, updater-flashed ELF/DfuSe parity,
the physical4096-byte C2 reserve, resource/FAP/archive checks and anonymous public
asset verification. CI does not perform hardware-only checks. Confirm lock/PIN,
sleep/wake, held shortcuts, app return and retained settings on owned devices.

Classic remains available as the ordinary desktop. Known public Stable010 and
Dev011-006 remain immutable rollback releases; do not mix their old private FALs
with a newer core. Preserve backups before any downgrade. Existing protected-app
and Community ABI findings are not cleared by this release.
