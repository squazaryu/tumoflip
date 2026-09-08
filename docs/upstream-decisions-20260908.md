# Upstream reconciliation, 2026-09-08

## Released coverage and operator decisions

- GUI #1125 (`56ec776a04`) is selectively implemented by `88981e5543`,
  released in Dev 008-018 (API 88.5). Broader menu plugin architecture is separate.
- Loader #1129 (`66435ce292`) was explicitly rejected by the operator on
  September 7. Do not requeue it under #445 without a new explicit request.
- Analyzer #1131 (`7f76ec441f`): notification ownership fix `dcb20acd6c`
  shipped in Dev 008-019. Plugin extraction remains deferred in #462.
- Main dependency commit `19ffe4aea4` is content-equivalent on all three
  affected files to dev's existing #453 (`85099d978f`). Merging main records
  ancestry without changing the dev tree. The README's general API labels
  are corrected to 88.5; stable v1.0.7 references must retain 88.4.

## ARF history recovery

Tracked branch: D4C1-Labs/Flipper-ARF `main`, not `dev`.
Current upstream head: `a483f253c710b9e27654d28eec08022f3989c936`.
Former reviewed head: `56701a815b54121a4b4d102e361add5468ffa92b`.
The new head is an ancestor of the former head. This is a three-commit rewind,
not unrelated replacement history. Both objects remain available locally.

Removed from main:

1. `8117e422d0`: changes to the car-emulation scene.
2. `833c9ad29b`: Custom Emulate receiver D-pad behavior.
3. `56701a815b`: experimental BLE Central/scanner/jammer and KARR code,
   build/API/radio changes; its own message reports broken mobile connectivity.

No newer main-branch feature exists in this interval. Preserve existing local
implementations and prior rejection/hardware decisions. Resume incremental main
monitoring from a483f253, retaining the removed refs in a separate rewind record
so reappearing commits do not become unreviewed new work. The reason the author
rewound the branch is not proven by Git history alone.
