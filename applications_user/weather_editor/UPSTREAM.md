# Weather Editor import

Source: https://github.com/D4C1-Labs/Flipper-ARF
Imported directory: applications/system/Weather-Editor-EN
Introduction: 6fed312cf32acc8e298e8a06156b4a4cce1f76ee
Reviewed snapshot: 9fabe2de18971eaf35e3bf667cde17e3d0ec560a
The source repository distributes this tree under GPL-3.0.

This is an integration candidate, not a published FW Package. Local changes
acquire the Tumoflip radio lease before initializing hardware and release it
after workers and devices are stopped. The candidate is package-only so it
does not enter updater resources implicitly.

Local safeguards also disable the hidden About-to-Lab entry, preserve Lab
files during ordinary start/exit, check field lengths before narrowing them,
and reject truncated CC1101 custom presets. RAW, profile and WS saves use
create-new semantics, check close results and try to remove only a newly
created incomplete output. Failure to remove such an output remains a save
failure; existing files are never cleanup targets.

The host tests inject write/close failures and exercise field/preset bounds.
They do not establish full parser coverage or hardware acceptance.

Profile loading parses into private state and commits only after success;
a parser failure does not replace the active capture or preset.

Pending before catalog inclusion: radio hot-unplug and lease contention tests,
visual review, Community Apps duplicate/provenance audit,
and physical sensor acceptance. The disabled Lab sources remain in this
candidate and must not be presented as a supported feature.
