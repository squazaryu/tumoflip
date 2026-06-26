# Garage Door Remote

Tumoflip packages this ARF app as an isolated external app under
`/ext/apps/ARF Tools/garage_door_remote.fap`.

The import is source-only. Upstream `dist` artifacts are intentionally excluded.
Embedded protocol plugins use tumoflip-specific `.fal` names so they do not
collide with the existing ProtoPirate app.

Use only with garage/gate devices you own or are explicitly authorized to test.
