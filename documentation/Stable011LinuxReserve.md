# Stable011 Linux reserve preflight

Before any Stable tag was created, implementation PR607 Linux run46067423996
passed the diagnostic profile but failed the normal updater's existing strict
reserve gate:

    physical=0, sections=4076, DFU container=3767, required=4096 bytes

The C1 erase boundary therefore consumed the reserved page: the section end was
20 bytes past the permitted C1 page. This is not proof of a behavior regression
or an unsafe C1/C2 overlap in the owner-tested local image. The exact Linux image
must still satisfy the full4096-byte reserve before publication.

The owner explicitly approved shortening only a few service diagnostic phrases,
with identical arguments, conditions and functionality, then repeating the full
Linux tests/build. No reserve/compiler profile relaxation or tag replacement is
authorized. The Stable pre-tag build additionally uses the actual Stable identity
so a short diagnostic SHA cannot mask an overflow.

Failure source57bd08ddbbc0095bda733b58810ed4c7554ea982:
https://github.com/squazaryu/tumoflip/actions/runs/46067423996.

The small follow-up is confined to SavedStruct, GUI input warnings and BLE stack
version logging. It does not change stored headers, input routing, BLE calls or
their parameter order. Full CI and final tagged-source checks remain mandatory.
