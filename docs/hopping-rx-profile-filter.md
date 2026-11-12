# Standard Sub-GHz: profile-only presets and automatic hopping

Fix for a defect present in Dev009-034: the manual modulation picker hid
`TumoHonda`, but Preset/Combined hopping fell back to every registered preset.
The Honda profile is registered in that same setting table for explicit RX
selection, so it was silently visited even while scanning with ordinary Core.
The protocol-pack selection and radio-preset selection are independent.

## Scope

- Filter the built-in Honda RX setup only from the **automatic fallback** when
  no `Hopping_Preset` list is configured. Map filtered ordinals back to actual
  setting indices without deleting/reordering the underlying preset table.
- Remember whether registration/validation of the built-in profile succeeded,
  then resolve its current table index by name. A user collision rejected by the
  catalog remains eligible, even if it differs only in TX power. Changing the
  PA table after successful registration does not lose the ownership marker.
- Preserve all explicit `Hopping_Preset` entries, including order, duplicates
  and deliberately selected Honda presets. This is not a restriction on user
  configuration.
- Keep frequency-only hopping on the selected preset, including an explicitly
  selected Honda profile. Preserve default/user modulation order, RSSI hold,
  dwell, wrap, Combined frequency progression and AUTO diversity radio loads.
- Keep the Honda 433/315 profile catalog and its existing registration/collision
  policy unchanged. No protocol is removed, no settings/.sub files are rewritten,
  no SD migration, public export or API version changes. One private ownership
  flag uses existing tail padding; a compile-time check preserves the struct
  size and all prior member offsets.

Only Standard's resident automatic hopper changes. ARF's separate hopper does
not automatically register this profile and is unchanged. This patch requires
a new firmware build for installation; it does not replace the immutable034
release and does not require an independent FW Packages or iOS content update.

The private registration implementation and its46-byte preset live once in
`subghz_rx_profiles.c`, instead of being duplicated by header inclusion in the
allocator and UI. ProtoPirate links that private source into its own native FAP;
no new firmware SDK export is introduced. This behavior-preserving factoring
retains the full4096-byte C2 reserve without removing functions.

## Verification

`test_hopper_profile_filter` compiles the actual production hopper and RX catalog
with fatal ASan/UBSan, replacing radio/storage operations with spies. It covers
the profile at every list position, arbitrary custom presets, name collisions,
PA edits, explicit lists, 433/315 selection, empty/stale indices, all hopping
modes, RSSI/stop states and both diversity radios. It snapshots the setting table
to catch filter-induced rewrites. Hardware RF behavior is not simulated.

RED checkpoint `54403f1a85` reproduces default, after-profile and no-eligible-list
failures before the fix. The PA-table reproducer also failed on the old code.
Both the new suite and existing RX-profile tests are mandatory in PR/release CI.
Run full host discovery and exact F7/updater validation with the unchanged4096
physical C2 reserve. The optional host coverage mode is
`HOPPER_HOST_COVERAGE=1 python3 -m unittest tools.tumoflip.test_hopper_profile_filter`.

Device acceptance remains: select Core and cycle Preset/Combined through a full
frequency round; confirm only ordinary presets are visited, then stop hopping
and explicitly choose Honda433/315. Also test an explicit custom hopping list,
frequency-only mode and AUTO diversity. No user device is flashed by this fix.
