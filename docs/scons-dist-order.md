# Distribution ordering correction

Selective tooling adaptation of official Flipper firmware #4457, commit
`2bbf7a54bae7316da858ab62f22ed8db0b4c661b`.

`scripts/sconsdist.py` clears the selected flavor's output directory before
creating firmware distributions. Previously `fap_dist` only depended on app
validators. Under a combined parallel invocation, FAP/debug files could be
installed first and then removed by the firmware distribution step.

Each `DistCommand` now records its alias, direct pseudo-target and action nodes.
FAP installation requires completion of every explicitly selected dist command.
Unselected commands are not pulled into the graph. Validator dependencies are
unchanged. This adds ordering edges only: no runtime code, API, application
source or SD cleanup rule changes. User captures on the device are outside scope.

The regression fixture uses the actual production `DistCommand` implementation
and FAP installation stanza with SCons, not a mocked scheduler. Tiny disposable
artifacts and a deliberately slow firmware producer reproduce the late wipe.
Cases cover both CLI orders, direct targets, multiple selected commands,
apps-only/firmware-only selection, empty app sets and validator failure.
The initial graph lost FAPs in four combined-build cases; after the fix all cases
pass. Distribution-only and apps-only cases must retain their original scope.

Run using the pinned toolchain Python (which includes SCons):

```sh
toolchain/current/bin/python3 -m unittest tools.tumoflip.test_dist_order
```

The same check runs after toolchain initialization in PR and stable-release CI.
No new firmware version, API bump or FW Packages/iOS release is needed for this
tooling-only correction. Existing erase-aligned ELF/DFU C2 validation remains
authoritative; the upstream Drone cache/download recipe is not imported.
