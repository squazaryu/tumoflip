# Sub-GHz Wardriving

Sub-GHz receiver and recorder with optional UART GPS metadata. The Tumoflip
variant supports GPS Off, NMEA, and U-blox modes for external modules such as
Module One.

The source was imported from `xMasterX/all-the-plugins` at commit
`c11b32c5db55650e356a147f0e4d5bfc2b896fac` under GPL-3.0 and adapted for the
Tumoflip API 88 contract. The unsupported Companion GPS RPC mode is omitted;
the UART GPS plugin remains embedded in the FAP.

Build with:

```sh
./fbt -j2 fap_subghz_wardriving
```
