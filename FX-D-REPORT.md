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

---

## RESULTS (appended 2026-10-17T10:55Z; nothing above this line was edited)

AI-generated: Claude Opus 5.5, effort not exposed (requested xhigh). Advisor gpt-6-astra (high) checked the patch design before code; review below.

FX-D takes the source selector clone at fusion completion from 4.76 s to 0.026 s at 590 M lids (n=10 each, M4 Max VM, 2 MiB pages; 0.114 s with 4 KiB pages), with identical state digests on all 553 compared cases. This removes the bottleneck the B3 profile found, but only in the local clone measurement. Whether production's master-thread block, the index thread's wait on `_index_update_lock`, feed stalls and 429s drop with it is unmeasured, and so is Graviton.

| 590 M lids, `cloneAndSubtract` wall, one clone per fresh process | median | IQR | ns/lid | n | vs A |
|---|---|---|---|---|---|
| A base 8ec58e1230 (per-lid `addDoc`, fill, remap) | 4.764 s | 4.702–4.774 | 8.07 | 10 | – |
| B bulk constructor only (ddd975bf17) | 1.468 s | 1.453–1.470 | 2.49 | 10 | −69.2 % |
| C0 bulk + fused remap, scalar (not shipped) | 1.012 s | 1.006–1.016 | 1.72 | 10 | −78.8 % |
| **C bulk + fused vectorised remap (9375939a23)** | **0.0258 s** | 0.0257–0.0262 | **0.044** | 10 | **−99.5 %, 185×** |
| C, THP=never (4 KiB first-touch faults) | 0.114 s | 0.113–0.114 | 0.19 | 5 | A in same arm: 5.022 s |
| reference: `memcpy` 590 MB into a fresh Vespa `Alloc` | 0.0167 s | 0.0164–0.0172 | 0.028 | 10 | floor |

Rows A, B, C0 and C come from the same alternated run (A B C0 C per round, 10 rounds, `bench-20261017T103602Z.txt`). Every run of every variant produced the same content hash `bfc19397151919d7`. Noise: the IQR is ≤ 2 % of the median for each of the four clone rows (C: 0.5 ms on 25.8 ms); there was no separate base-vs-base arm.

### Verdict per pass rule

| rule | result | evidence |
|---|---|---|
| C1 existing tests green | **pass** | 9/9 test binaries on A, B, C0, C (`ctest-*.txt`): sourceselector, sourceblender, attribute, extendattribute, profiled_iterator, searchcore fusionrunner, indexmanager, indexcollection, diskindexcleaner |
| C2 cross-build identity | **pass** | 553 cases (9 constructor, 544 clone incl. 4 at 590 M), 617 digest lines and 55 load warnings identical across A, B, C0, C; 0 mismatches. Each line holds numDocs, committed limit, docIdLimit, default and base id as values, and FNV-1a 64-bit hashes of every byte `[0, numDocs)`, of the saved `.dat` data after its header and of the reloaded selector, plus the header tags. Hash equality, not a byte-by-byte comparison. Uncommitted limit, which has no getter: `committedAfterCommit` (one more `commit()`, which would raise the committed limit to it) is identical too, e.g. 584,100,000 < numDocs in the compacted 590 M case. Final run `dump4-*` with the review-fixed program; first run `dump-*`, same digests |
| C3 assert at ≥ 254 | **pass** | death tests (255 with diff 1, 254 with diff 0 abort; 255 with diff 2 doesn't) pass on A and C; `__assert_fail` is present in official 8.679.50 `cloneAndSubtract`, so asserts are live in production |
| C4 failing-first | **pass** | on A: 13/14 new tests pass, `generation_count_does_not_depend_on_lid_count` fails (constructor 19 vs 20, clone 20 vs 21 generations at 1 Mi vs 4 Mi lids); passes on B and C. The `init_docs` API test cannot compile on A and passes on C. What it proves: when `init_docs` reallocates while a reader holds a generation guard, the old buffer goes on hold (on-hold bytes > 0) and is freed after the guard is released (0). It does not test concurrent readers of the new selector, which can't exist during the clone (I1) |
| P1 ≤ 0.5 s at 590 M | **pass** | 0.0258 s (THP), 0.114 s (4 KiB pages) |
| P2 n ≥ 10, alternated, per phase | **pass** | table above and phase table below; THP=never arm n=5 |
| P3 disassembly | **pass** | below |

| hypothesis | result |
|---|---|
| H1 base ≥ 3 s, grow ≥ 70 % | **half**: 4.76 s, but grow is 65 % (grow + default fill 69 %) |
| H2 bulk init alone removes ≥ 70 % | **missed by 0.8 points**: −69.2 % |
| H3 fused ≤ 0.5 s, ≤ 0.85 ns/lid | **pass**: 0.044 ns/lid |
| H4 baseline fences/calls per lid, candidate vectorised | **baseline pass; candidate failed as first written**: GCC 15.2 left `s > d ? s - d : 0` scalar and branchy (C0, 1.72 ns/lid). Rewritten as `s - min(s, d)`, which vectorises (C) |

### Phases at 590 M lids (A from the real `addDoc` loop and constructor; n=10 each)

| phase | A | B | C |
|---|---|---|---|
| grow (`addDoc` × n, fresh attribute) | 3.096 s, 5.25 ns/lid | 0.011 s (grow + fill together) | 0 (grow + remap together) |
| default fill (constructor − grow) | 0.188 s, 0.32 ns/lid | – | not done in the clone |
| remap (clone − constructor) | 1.480 s, 2.51 ns/lid | 1.456 s, 2.47 ns/lid | 0.026 s for grow + remap |
| minor page faults in the clone | 1,312 | 282 | 282 |
| peak RSS added by the clone | 844 MB | 564 MB | 564 MB |
| generation of the new attribute | 33 | 3 | 2 |

Phase splits subtract medians from separate processes, so they are approximate. With 4 KiB pages, C's 144,043 faults cost about 0.8 µs each, which is most of its 0.114 s.

### Disassembly (aarch64, `objdump -d`)

Official `vespa-8.679.50-1.el8.aarch64` (`libvespa_searchlib.so` sha256 2ba2cc42…, GCC 15.1.1), static common path per lid:

| path per lid | instructions | `dmb` | `ldar` | calls | vectorised |
|---|---|---|---|---|---|
| grow: `reserve` loop 6, `addDoc` 46, `isMutable` 4, `reclaim_unused_memory` 19, `update_oldest_used_generation` 18, `reclaim_memory` 4, 4 PLT stubs 16 | ~113 | 1 (`dmb ish` after the push_back) | 1 | 5 (4 PLT, 1 indirect `br`) | no |
| default fill loop in `reserve` | 8 | 0 | 0 | 0 | no |
| remap loop in `cloneAndSubtract` | 20–22 + data-dependent branch | 0 | 1 (`_vector_start` reloaded per lid) | 0 | no |
| **candidate C remap+grow** (local GCC 15.2.1 build) | **8 per 16 lids = 0.5** (`ldr q`, `umin .16b`, `sub .16b`, `str q`, `umax .16b`, add, cmp, b.ne) | 0 (1 per clone, after the loop) | 0 | 0 | yes, 16 B |

The profile's `isMutable` leaf comes from `addDoc` → `defaultValue()` → `isMutable()` through the PLT. The baseline numbers are static path counts, not retired-instruction counters: the vz VM exposes no PMU.

**Why C is 1.5× `memcpy`, and why there's no hand-written NEON.** Kernel-only on pre-faulted buffers (`kernel-only-2.txt`, n=10): shipped kernel 19.95 ms, the same loop without the max 11.42 ms, `memcpy` 11.31 ms. Dropping the max closes the gap, which fits a single loop-carried `umax` accumulator costing ~2.5 cycles per 16-byte iteration. That is the leading explanation, not a proven one: there's no PMU in the VM to confirm the stall. End to end, C's clone is 9 ms over `memcpy`, about the same as this kernel gap; selector construction and commit were not timed separately. A 64-lane accumulator array made GCC spill to the stack every iteration (35.9 ms), and hand-written NEON would add architecture-specific code to save ~9 ms out of 4.76 s on M4. Graviton's lower per-core bandwidth probably hides the chain, but that is inferred, not measured. Not shipped.

### Changes (branch `agents/fx-d-source-selector-bulk-init`)

- ddd975bf17 `SingleValueNumericAttribute::init_docs(num_docs, fill)`: empty attribute only (asserted), one `RcuVector::reserve` (old buffer onto the generation hold list), `unsafe_resize` without initialization, nothrow fill (`static_assert`), one release fence, `setNumDocs`, `updateUncommittedDocIdLimit`, one `incGeneration` if it reallocated, else `reclaim_unused_memory`, as `addDoc` does. `get_values_for_writer()` for the source bytes. `FixedSourceSelector`'s constructor uses it with a `std::fill`. `setSource → reserve → addDoc` is unchanged.
- 9375939a23 `cloneAndSubtract` builds the new selector with no initial docs, then `init_docs(N, subtractSources)` writes `s − min(s, min(diff, 255))` per byte from the source bytes and tracks the max; `assert(maxSource < SOURCE_LIMIT)` before `commit()`; base id and committed limit as before.
- 387a7982ce the dump/benchmark program (`searchlib_sourceselector_clone_benchmark_app`), using only pre-change APIs so it builds against the base.
- Allowed differences, measured: generation (2 instead of 33 at 590 M) and capacity. Capacity is now `calcNewSize(n)` ≈ 1.5 n rounded up to 2 MiB, as `AttributeVector::addDocs` already does, instead of the push_back staircase. Cost: the attribute's allocated bytes at 590 M go from 660.6 MB to 887.1 MB (+226.5 MB, +34 %; `dump-big-*.txt`). The extra is never touched, so it isn't resident: peak RSS added by the clone is 564 MB instead of 844 MB. Traced, not measured: proton's memory feed block reads anonymous RSS from `/proc/self/statm` (`ProcessMemoryStats`), the selector attribute isn't in any attribute manager, and `IndexMaintainer::getFlushStats` counts `docIdLimit × 1 byte`. I found no reader of the selector's allocated bytes. Process virtual size (`_virt` in the same stats) does grow by ~226 MB per selector. Gain: after a clone, the next `setSource` beyond N does not reallocate and copy 590 MB on the index thread (test `leaves_room_to_grow`).
- Correction made during the work: I first read baseline's 20 → 21 generation step after a post-clone `setSource` as a reallocation. It is the commit (`~ValueModifier` bumps the generation on every commit). The test now asserts exactly one bump.

### Setup

Apple M4 Max (16 cores, 128 GB) host; lima 2.2.1 vz VM `fxd-build`, Ubuntu 26.04, kernel 7.0.0-34, 12 vCPU, 64 GiB, THP `madvise` (Vespa's `Alloc` calls `madvise(MADV_HUGEPAGE)` on every mmap). Build: flattened `vespaengine/vespa-build-almalinux-8@sha256:d705ce8b5027…` (GIT_REF 7c7a29e2, created 2026-10-02) in a chroot, GCC 15.2.1 20260123, CMake 3.30.9, Vespa default flags `-g -O3 -fno-omit-frame-pointer -march=armv8.2-a+dotprod+crypto -mtune=neoverse-n1`, no NDEBUG. `configgen.jar` built from the same source with Maven 3.9.16. Benchmarks `taskset -c 2` (vCPU only; macOS can't pin vCPUs to P-cores). The VM was otherwise idle (load 0.5–1.0), but the host was not: the shared podman VM's Vespa stacks kept the macOS load average at 5.5–8.5 during the runs. The tight IQRs suggest little effect, but it is uncontrolled. Variant libraries: A da5b4a5e…, B 89ea9a1c…, C0 a931ba81…, C 7a6f9c80… (`sha256-*.txt`); each benchmark line prints the library it loaded.

### Not covered

- **Graviton.** Every timing is M4 local evidence. B3 report 10 saw 6.3–7.0 s on-CPU in the profile plus ~1.3 s before attach, at ~591 M lids (inferred from `content_hash.dat`, not measured), against 4.76 s here for the same code path. So the baseline is very roughly 1.3–1.7× slower per lid on Graviton. I don't know the factor for C, whose cost is memory bandwidth and page faults rather than instructions.
- **Production THP setting.** Unknown. With THP `never`, C is 0.114 s on M4.
- **End to end**: feed stalls, 429s, `Slow processing` lines and `doneFusion` total time (it also calls `replaceAndRenumber` and `swapInNewIndex`) were not measured. Only the clone was.
- `FixedSourceSelector::load` + `capSelector` on the fusion thread (also per-lid) is unchanged.
- No x86 build of this branch was run. The kernel is portable C++ and vectorises on aarch64 GCC; x86 codegen is unchecked.

### Option (b), design note only (not implemented: step 4 met the target)

Move the copy to the fusion thread and leave the master only a tail. The fusion thread would snapshot the source bytes `[0, N0)` under an attribute read guard plus `_index_update_lock` for the uncommitted region, and build `clone0` with `subtractSources`. The index thread would record every lid it passes to `setSource` after the snapshot, plus `compactLidSpace` calls (append-only vector under `_index_update_lock`). In `doneFusion` the master replays the log into `clone0`: each logged lid gets `getNewSource(_current_index_id, diff)`, growth beyond N0 uses `init_docs`-style append, and compaction is reapplied. The master's cost then scales with feed volume during fusion instead of corpus size. Costs: a new per-feed-op log, a lock-ordering argument across two threads, and a correctness surface (missed log entry = wrong source = wrong ranking) that C's 26–114 ms doesn't justify. Revisit only if a Graviton run puts C above ~0.5 s.

### Review

Code review by gpt-6.1-sol, high effort (`task` code-review; `models-used.mjs` shows it was selected by task argument but has no usage record to confirm what ran) on 26b890b586: one finding, medium. The dump program printed `save=FAILED`/`close=FAILED` but still exited 0, so two builds failing identically could pass the comparison. Fixed in 387a7982ce: open, close, short-read and save failures set a flag and exit 1. All dumps were then re-run with the fixed program on A, B, C0 and C, including the 590 M cases: `persistence_failed=0` and 0 `FAILED` strings in all 8 files, with identical digests (`dump4-*`). A forced ENOSPC (512 KiB tmpfs as working directory) makes Vespa's file writer throw first, and the program exits 134 (`forced-persistence-failure.txt`). So a failed save can't pass silently, but the flag paths themselves were not triggered by a test. The reviewer found nothing in the library change. It ran Apple Clang syntax checks for arm64 and x86-64 on both translation units, not a build or tests. The timings above came from the benchmark binary before this fix, which only touches dump code. Every measured revision can be rebuilt from `fx-d-artifacts/variants/` (patches + hashes in `MANIFEST.txt`).

### Next steps

1. Graviton lab run of the same program: one Graviton node of the production generation, ~1 h including build. Needs HQ's lab lock and Illia's go; request sent with a default.
2. Canary: one content node on a build with ddd975bf17 + 9375939a23. Read the next fusion completion's `Slow processing` line and the S0 feed/429 rows only, no tracer. Kill criterion: any `assert` abort in `cloneAndSubtract`, or a `doneFusion` block not lower than before. Rollback: redeploy 8.679.50. Owner: not set.

### Reproduce

`fx-d-artifacts/scripts/`: `setup.sh` (chroot), `variant.sh A|B|C`, `test-and-save.sh`, `failing-first.sh`, `bench.sh` (`N=10 DOCS=590000000`), `stats.py`, `kern.cpp`. Raw outputs: `fx-d-artifacts/results/`; disassembly: `fx-d-artifacts/disasm/`.
