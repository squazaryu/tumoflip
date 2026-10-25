# Recoverable Desktop frame allocation

## Scope

Fix the optional animation-loading failure seen with a35268-byte request,
100448 free bytes and only31784 bytes in the largest free block. Total free RAM
does not imply that a single large allocation can succeed. This is unrelated to
SD capacity or the physical flash reserve. It is not proof of every idle-crash
cause or the identity of a particular animation.

## Behavior

- A private resident-only allocator shares the actual FreeRTOS protected heap
  search, splitting, coalescing, zeroing, statistics and scheduler locking.
  An unsatisfied optional request returns NULL. There is no free-space-check /
  fatal-allocation race and no new SDK export or API bump.
- Ordinary malloc/calloc users remain fatal on OOM and retain the original
  requested byte count in diagnostics. Zero-size mandatory requests and ISR
  misuse remain programming errors. The optional failure path does not invoke
  the malloc-failed hook or overwrite a retained crash report.
- Animation frame owners and legacy per-frame buffers use the optional path.
  The fast whole-bundle read is retained when it fits. Existing legacy `.bm`
  fallback is retained, and the idle selector uses its existing built-in
  animation fallback when external frames cannot load. Animation files and user
  choices are not deleted or globally disabled.
- A failed metadata parse after frame loading now invokes the full animation
  destructor, releasing frames as well as frame order and metadata. Partial
  frame loads also release every successfully allocated buffer.
- Playback/resume view code, shortcut retention policy and the deferred retained-
  frame feature are unchanged. This is optional-resource failure handling, not a
  global policy that suppresses OOM for mandatory allocations or heap corruption.

## Verification

Native ASan/UBSan tests exercise the actual protected FreeRTOS allocator and the
actual Desktop loader/cache/idle-selector functions. Cases include the retained
fragmented-heap shape, cold initialization, exact fits, coalescing, zero/overflow/
ISR policy, failed hooks, truncated/corrupt bundles, missing legacy files,
per-frame failure, late metadata errors, built-in fallback and successful retry.
Coverage selection matches production function bodies, excluding allocator/GUI
stubs. No physical OOM is deliberately injected.

Full firmware tests, all FAP/FAL builds and metadata checks, API stability and
strict4096-byte C2/update validation remain release gates. Physical-device
acceptance must cover normal animations and ordinary long idle use after installing
the new Dev. Short host tests cannot certify week-long stability. Preserve the
old crash record: a new incident must be distinguished by its sequence/build.

FW Packages027 remain ABI-compatible; no managed FAP or iOS code change is needed
for this firmware-only fix. No device or ESP32 is flashed by implementation or
publication.
