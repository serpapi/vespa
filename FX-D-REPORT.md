# FX-D: build the fused source selector in bulk instead of one lid at a time

AI-generated: Claude Opus 5.5, effort not exposed (requested xhigh). Child of a50a503d / Search Index HQ 06a46e65, board TASK-88.

Status: PREREGISTRATION ONLY (written 2026-10-17T09:46Z, before any build or measurement). Results are appended below the line "RESULTS" and never edit this section.

## Base

Branch `agents/fx-d-source-selector-bulk-init` off `agents/serpapi-8.679.50` 8ec58e1230. `git diff --quiet v8.679.50 HEAD -- searchlib/src/vespa/searchlib/attribute searchlib/src/vespa/searchlib/common searchcore/src/vespa/searchcore/proton/index searchcorespi vespalib/src/vespa/vespalib/util` exits 0, so every file this change touches equals tag v8.679.50 (a102426624). The 19 files that differ from the tag are rank features and the strict source blender.

## What the code does today (read, not measured)

- `FixedSourceSelector(default, name, k)` calls `reserve(k)`, which calls `SingleValueNumericAttribute<int8>::addDoc` k+1 times. Each call: `RcuVector::push_back` (a full 1.5x reallocation + copy when capacity runs out, old buffer put on hold), `atomic_thread_fence(release)`, `incNumDocs`, `updateUncommittedDocIdLimit`, then `incGeneration()` on a reallocation or else `reclaim_unused_memory()`, a virtual call into `GenerationHandler::update_oldest_used_generation` + `reclaim_memory` + `GenerationHolder::reclaim`. Then a second loop `set(i, default)` over `[committed, k+1)`, then `commit()`.
- `cloneAndSubtract(name, diff)` constructs with `k = N-1` (N = source `getNumDocs()`), so all N lids are grown and default-filled, then a third pass does `get → getNewSource → assert(< 254) → set` per lid, `commit()`, `setBaseId(base+diff)`, `setCommittedDocIdLimit(C)` (C = source committed limit).
- `doneFusion` runs on the master thread and holds `_state_lock` and `_index_update_lock` around the clone. `putDocument`, `removeDocuments` and `compactLidSpace` take `_index_update_lock` on the index thread, so the clone time is also an index-thread stall, not only a master-thread one. `FusionRunner` clones a freshly loaded, thread-local selector.

## Invariants the change must keep

- **I1 Visibility.** The new selector is unreachable by any other thread until `cloneAndSubtract` returns: `doneFusion` assigns `_selector` afterwards and publishes it via `replaceAndRenumber`/`swapInNewIndex` under `_new_search_lock`; `FusionRunner` keeps it local; a constructor's object is not shared yet. So the new attribute has no reader and no generation guard while it is filled.
- **I2 Source stability.** The source selector's only writer (index thread, `setSource`/`compactLidSpace`) holds `_index_update_lock`, which `doneFusion` also holds; queries only read. Reading the source's raw bytes `[0, N)` during the clone is race-free.
- **I3 Clone state.** After `cloneAndSubtract`: `getNumDocs() == N`; for every lid `i < N`, including the sentinel lid past the last set doc and lids at or above C after `compactLidSpace`, `byte[i] == (src[i] > diff ? src[i] - diff : 0)` with `src[i]` read as uint8 and diff as uint32; committed limit `== C`; uncommitted limit 0; default `== getNewSource(default, diff)`; base id `== base + diff`. N = 0 gives an empty selector with committed limit C (= 0).
- **I4 Assert.** A result `>= SOURCE_LIMIT (254)` still aborts. Vespa's CMake never defines NDEBUG (grep: 0 hits), so this assert is live in production builds.
- **I5 Constructor.** `FixedSourceSelector(default, name, k)` with `k != UINT32_MAX` gives `k+1` docs, every byte `== default`, committed limit `k+1` (`getDocIdLimit() == k`).
- **I6 Live growth unchanged.** `setSource → reserve → addDoc`, `compactLidSpace`, `load` and `capSelector` keep their current code.
- **I7 RCU.** Any reallocation in the bulk path goes through `RcuVectorBase::reserve → expand`, so an old buffer goes on the generation hold list; the generation is bumped when a reallocation happened and memory is reclaimed otherwise, as `addDoc` does; values are written before a release fence and become visible only through the release store of the committed limit in `commit()`. Allowed differences: the new attribute's generation number (one bump instead of one per reallocation) and its capacity (the `AttributeVector::addDocs` rule, `calcNewSize(n)`, instead of the push_back staircase). Both are reported, neither is in the byte comparison.
- **I8 Save/load.** Identical state gives identical saved data bytes and an identical reloaded selector.

## Hypotheses

- **H1.** Baseline `cloneAndSubtract` at 590 M lids takes ≥ 3 s on this M4 VM and ≥ 70 % of it is the grow phase (per-lid `addDoc`).
- **H2.** Bulk init alone (constructor grows once and memsets the default) removes ≥ 70 % of baseline clone time.
- **H3.** Bulk init plus one fused remap pass (read source once, write destination once) brings the clone to ≤ 0.5 s at 590 M lids, about ≤ 0.85 ns/lid, bounded by first-touch page faults and memory bandwidth, not by instructions.
- **H4 (disassembly).** Official 8.679.50 aarch64 `addDoc` executes ≥ 1 `dmb` and ≥ 2 non-inlined calls per lid and nothing in the clone path is vectorised; the candidate's remap loop has no `dmb`, no call, and uses 16-byte NEON (`uqsub v.16b` or equivalent).

## Pass rules

Correctness (all must pass; a miss blocks the change):
- **C1** Existing tests green on candidate: `searchlib_sourceselector_test_app`, `searchlib_attribute_test_app` family that covers single numeric attributes, and searchcore `fusionrunner_test`, `indexmanager_test`, `indexcollection_test`.
- **C2** Cross-build byte identity: the same dump program built against baseline and candidate prints identical digests for every case: sizes {0, 1, 2, 63, 64, 65, 4095, 4096, 4097, 1 M, 590 M} × patterns {all default, random 0..7, random 0..253, undefined byte 0x80 present, compacted (C < N), sentinel past last set doc} × diff {1, 2, 7, 200, 253, 254, 255, 256, 1000}, where each digest covers numDocs, committed limit, docIdLimit, default, base id, a hash of every byte `[0, N)`, a hash of the saved `.dat` data after its header, and a hash of the reloaded selector. 590 M runs the two realistic patterns only (random 0..7, compacted), diffs {1, 7}. 0 mismatches out of all cases, and the case count is printed and nonzero.
- **C3** Bytes 254 and 255 in the source (set through the attribute directly) abort the clone on both builds when the result is ≥ 254 (death test), and don't when it isn't.
- **C4** New failing-first tests fail on baseline and pass on candidate: the clone bumps the new attribute's generation O(1) times (baseline: one per reallocation, ≈ 17 at 1 M lids), and the new bulk API keeps an old buffer alive while a reader guard holds it (on-hold bytes > 0 until the guard is gone, 0 after).

Performance (local evidence on an Apple M4 Max lima VM, not Graviton numbers):
- **P1** Candidate (bulk + fused remap) `cloneAndSubtract` at 590 M lids: median wall ≤ 0.5 s.
- **P2** Reported for baseline, bulk-init-only and bulk + fused remap, each from ≥ 10 alternated runs (A B C A B C …), one clone per fresh process so peak RSS is per run: median and IQR wall, ns/lid per phase (grow, default fill, remap), minor page faults and peak RSS. Grow and default fill for baseline come from timing the real `addDoc` loop and the real constructor; remap = clone − constructor.
- **P3** Disassembly counts (instructions per lid, `dmb` per lid, calls per lid, vectorised yes/no) for official 8.679.50 aarch64 and for the candidate build, from objdump of the actual libraries.

## Run protocol

- Build: `vespaengine/vespa-build-almalinux-8` arm64 (digest recorded), Vespa default flags, inside a dedicated lima VM `fxd-build` (vz, 12 vCPU, 64 GiB) on the Apple M4 Max. Compiler, flags, image digest and SHAs recorded.
- Variants: A = 8ec58e1230 (base), B = bulk-init commit, C = bulk + fused remap commit; each variant's `libvespa_searchlib.so` and bench binary copied to its own dir with sha256; the bench prints the library path it loaded.
- Bench: `taskset -c 2` inside the VM (vCPU pinning only; the macOS host cannot pin vCPUs to physical cores, so P-core vs E-core placement is not controlled; noted as a limit). Nothing else running in the VM. Source pattern random 0..7 with 1 % compacted tail, seed 42, 590,000,000 lids, diff 1.
- Not run here: Graviton, production, feed stall and 429 effects.
