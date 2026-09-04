# Quac FW Packages ownership

## Capability

- **Capability name:** Quac independent package ownership
- **Source:** operator decision after Tumoflip Dev 008-015
- **Primary actor:** TumoCompanion FW Packages installer
- **Outcome after ship:** Quac is installed, updated and transactionally restored
  as a package overlay rather than bundled in firmware resources.
- **Success signal:** a firmware build excludes Quac from `resources.ths`; the
  matching package snapshot contains exactly the built Quac FAP at
  `/ext/apps/Tools/quac.fap`; installation never touches Quac user data.

## Product intent

Dev 008-015 is the last immutable firmware release that bundles Quac. Future
firmware builds keep the source and build target, but delivery and updates move
to FW Packages. Quac updates must not force a firmware release.

## Constraints

- Only `apps/Tools/quac.fap` becomes package-owned.
- `/ext/apps_data/quac`, saved signals, links, playlists and settings are
  user-owned. They are never manifest overlays, cleanup targets or migration
  inputs.
- The FAP is compiled from an exact Tumoflip commit and APPCHK-validated against
  firmware API 88.4. Branch names and Community Pack binaries are not inputs.
- Updater resources and `resources.ths` exclude Quac from the first firmware
  release after 008-015.
- The first package activation stages and verifies the new FAP before replacing
  an existing 008-015 copy. Transaction failure restores the previous FAP.
- Published firmware, catalog tags and release assets are never overwritten.
- Stable ownership is not changed by this dev migration.

## Actors and surfaces

- Tumoflip source: application manifest, package export/ownership contracts,
  build and release validation.
- FW Packages control plane: Quac allowlist, base group, exact dev release plan,
  provenance, catalog index and immutable release.
- TumoCompanion: generic overlay reconciliation, staged install, MD5
  verification, rollback and status refresh.

## States and transitions

- `008-015 firmware-owned -> dev package candidate -> dev package active`
- `package candidate -> validation failure -> no publication`
- `install staged -> verified -> activated`
- `install staged/activated -> failure -> previous FAP restored`
- `future firmware -> Quac absent until compatible FW Packages activation`

Selecting a catalog revision predating Quac ownership is not promised to
downgrade Quac; it must not delete the installed FAP or user data. Transactional
rollback of the first activation restores the pre-existing FAP bytes.

## Interface contract

- Source path: `apps/Tools/quac.fap`
- Device target: `/ext/apps/Tools/quac.fap`
- Group: `base`
- Build artifact: `build/f7-firmware-C/.extapps/quac.fap`
- Package metadata: exact bytes, SHA-256, MD5, source commit, target/API
  compatibility and package revision.
- Idempotency: matching bytes are up to date; mismatched bytes require one
  atomic install; repeated installs do not alter `apps_data`.

## Data implications

The FAP is catalog-owned. Quac runtime data remains device/user-owned and is
retained independently of install, rollback, firmware update or catalog
selection.

## Security and policy

The source allowlist and catalog allowlist must agree exactly. Any unexpected
path, dirty source, API mismatch, unreviewed source commit, extra changed target
or manifest/archive digest mismatch fails closed before publication.

## Non-goals

- No Marauder, Picopass/Loclass, Community Pack or shared RAW-worker change.
- No stable promotion or rewrite of Dev 008-015.
- No automatic deletion or migration of Quac data.
- No guarantee that installing firmware without FW Packages keeps the Quac FAP.

## Open questions

None block the dev migration. Hardware installation and rollback acceptance
remain separate after publication.

## Handoff

- **Ready for implementation?** Yes.
- **Needs architecture review?** Cross-repository contract review before publish.
- **Needs product clarification?** No.
- **Next lane:** TDD implementation followed by exact package publication and
  device acceptance.
