# Garage Door Remote

Tumoflip packages this ARF app as an isolated external app under
`/ext/apps/ARF Tools/garage_door_remote.fap`.

The import is source-only. Upstream `dist` artifacts are intentionally excluded.
Embedded protocol plugins use tumoflip-specific `.fal` names so they do not
collide with the existing ProtoPirate app. Their runtime plugin interface IDs
are kept under `gdr_*` as a source-level port of the upstream Garage Door
Remote rename, while the larger internal C symbol/file rename is intentionally
left out to avoid churn in this isolated Tumoflip import.

Use only with garage/gate devices you own or are explicitly authorized to test.
