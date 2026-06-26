# RollJam Standalone

Tumoflip packages this ARF RollJam 2.0 app as an isolated opt-in external app
under `/ext/apps/ARF Tools/rolljam_standalone.fap`.

It does not replace the existing lightweight `rolljam` child module used by
ARF Sub-GHz Full. The standalone app is kept separate so hardware testing can
decide whether it is stable enough to become the default RollJam path later.

Use only with devices you own or are explicitly authorized to test.
