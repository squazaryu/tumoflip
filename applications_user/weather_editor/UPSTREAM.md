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

Pending: persistence failure tests, radio power ownership, visual review,
Community Apps duplicate/provenance audit and physical sensor acceptance.
