# Historical JS test sources — not a runnable test target

These are byte-identical source fixtures from Tumoflip009-035, kept instead of
deleting the old JS test coverage when restoring the core on-device profile.
`origin.json` records original paths, source commit and SHA256 hashes.

JS Runner and its plugins are excluded by Tumoflip policy. Its private thread
and value helpers cannot be exported by the core test resolver. These fixtures
are therefore **not compiled, installed, executed or counted as passing tests**.
Their relative includes, module typings and storage paths retain the historical
layout; this directory is data for future adaptation, not a standalone build.

A future JS test host must own its private runtime/resolver and its helper
module without adding JS to core. Review cancellation, error-string ownership,
test-only storage cleanup and plugin/resource lifetime before running it. The
historical source is preserved, not endorsed as correct or hardware-accepted.

Do not expose the old `tests` SDK typings as a working production module.
