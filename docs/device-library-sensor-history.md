# Device Library, sensor hypotheses and file history — capability contract

This extends the approved Work Profiles / Decoder References candidate. It is
not permission to publish: show the new native GUI previews before release.

## Capability and constraints

- Device Library is a FW Package, not resident UI. Cards contain a name, notes,
  tags and up to eight SD file references; original files are never moved/copied
  just because a card links them. Known capture types can be opened explicitly in
  their normal app. Scripts/macros are references only, never executed implicitly.
- Sensor Helper extends a four-capture Sub-GHz set in Signal Workbench. The first
  three measured values train bounded hypotheses; the fourth is held out for
  validation. Report bit range, signedness, byte order, scale/offset and held-out
  result. No automatic protocol generation, TX, key recovery or claim of certainty.
- File History is opt-in. Once enabled, supported native Sub-GHz, Infrared and NFC
  save paths must snapshot an existing user file before destructive replacement.
  Missing history module / SD errors must fail the write, not silently bypass it.
  New files and unsupported types do not need a backup. Third-party writers are
  outside this contract. Recovery creates a new file, never overwrites the current
  one. Keep three recent versions per source, with a fourth staging slot, a size
  limit, checksums and explicit capacity/errors. Backups can contain private data.
- Prefer FAP/FAL implementation and bounded memory. Public API additions are not
  assumed. No region changes, radio transmission or monitoring/automation changes.

## Surfaces, state and data

Cards: list -> create/open -> edit metadata/link files -> save -> inspect/open link.
Card persistence uses checked versioned records; failed writes cannot invalidate
the previous committed card. Deleting a link does not delete its source file.

Sensor Helper: complete compatible RAW set -> enter four measurements in tenths
of a user-chosen unit -> evaluate -> inspect hypotheses -> export a new report.
Changing samples invalidates prior measurements/results. Unknown/truncated input,
equal training measurements and unsupported encodings cannot produce confidence.

History: disabled -> explicitly enabled -> snapshot existing source -> permit
editor write only after validated backup -> inspect versions -> restore a copy.
Partial/invalid records are not offered as usable versions; digest is checked on
restore. Retention removes only owned history slots, never original user files.

## Verification / handoff

Implement with native regression tests for parsing, fitting, held-out failures,
rotation and failed I/O; build firmware/FAPs; compare flash/RAM costs; render all
new screens and error states. Real-device acceptance remains separate. Package
routing and compatibility must be verified before publishing firmware/FW Packages.
