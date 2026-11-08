# REPORT: Vespa fork on 8.781.11, and a content node that restarts fast without fighting fusion

Status: DRAFT, sections marked TODO are filled as runs complete. Preregistration: `PREREG.md` (commit f0934d5e4c1,
written before any local run).

## 1. Fork brought to bleeding edge

- `serpapi/vespa` master synced with `vespa-engine/vespa` master on 2026-11-08 (`b74dc9e9d40`). Existing
  branches untouched.
- New branch **`serpapi-8.781.11`** = tag `v8.781.11` + the SerpApi patches, re-applied from their master-based
  originals (fewer conflicts than the 8.679.50 backports):

  | patch | commits | conflicts on 8.781.11 |
  |---|---|---|
  | string attribute label cache (`string-attribute-label-cache`) | 4 | none |
  | strict source blender (`strict-source-blender`) | 1 | none |
  | weighted set keys with `:` (`agents/dotproduct-colon-keys`) | 1 (+1 test-key follow-up) | none |
  | FX-D source selector bulk init (`agents/fx-d-source-selector-bulk-init`) | 3 | `RcuVector` was renamed `TypeStableVector` (same API) and `assert` became `CHECK` upstream; resolved by hand |
  | new: keep the patches' checks under `-DNDEBUG` | 1 | upstream now builds with `-DNDEBUG` (65bea555b53), which disabled the patches' `assert`s and broke the strict blender benchmark build |

- Build: `vespaengine/vespa-build-almalinux-8` (GCC 15.2.1, CMake 3.30.9), arm64, full C++ tree on a c8gd.16xlarge.
- Tests: the 13 test binaries that cover the patches pass; the whole `searchlib_*` + `searchcore_*` suite
  passes (320/320; `verify_feature` needs `--privileged` for `setarch` in Docker).
- Benchmarks, same binaries run against stock 8.781.11 `libvespa_searchlib.so` (sha 5397a1eaff56, from the
  official image) and the fork's (3434a71deabe), Graviton4 (c8gd), pinned to 8 cores:

  | benchmark | stock 8.781.11 | fork | |
  |---|---|---|---|
  | strict blender, 1 / 5 / 10 / 15 sources, ns per hit (median, 50M docs, 5M hits) | 15.9 / 21.1 / 29.7 / 35.7 | 8.6 / 8.8 / 9.1 / 9.3 | 1.8-3.8x |
  | same with 10 % stale postings and unpack | 56.6 / 61.6 / 70.6 / 77.4 | 44.0 / 44.3 / 44.4 / 45.0 | 1.3-1.7x |
  | source selector clone at 590M lids (n=3 each, identical byte digests) | 10.3 s | 0.041 s | 250x |
  | source selector constructor at 590M lids | 8.0 s | 0.021 s | 380x |
  | `tensorFromWeightedSet`, 1 thread, ms per query: vocab 1000 / zipf / heavy-tailed topical | 165.8 / 179.0 / 179.2 | 20.0 / 106.1 / 73.4 | 8.3 / 1.7 / 2.4x |
  | same, uniform labels / scattered tail (one run each) | 208.5 / 208.6 | 201.2 / 222.9 | 1.04 / 0.94x |

  The clone matters for restarts and fusion: `doneFusion` holds the index update lock for the whole clone, so
  stock 8.781.11 blocks feeding for about 10 s per fusion at production's lid count.

## 2. Production baseline (read-only)

Source: proton event lines in `vespa.log` of all 12 content nodes (r9g.16xlarge, 8.679.50 + fork backport on
content-1, ~465M docs per node), 2026-10-08..11-07, pulled with ansible ad-hoc `shell` (read-only).

### 2.1 Restart (content-1, 2026-10-18, the only proton start inside log retention)

| phase | seconds |
|---|---|
| document meta store load (17.4 GB) | 97 |
| attribute load, in parallel (slowest: `original_queries` 216, `path` 166, `content_hash` 79) | 216 |
| document store load | 113 |
| disk index open (932 GB fused index, mmap) | 0.011 |
| **components initialised** | **320** |
| transaction log replay, 14.57M ops at 11.5k op/s | 1,262 |
| **proton `started`** | **1,585** |

Stop time comes on top: 1.5-7 min measured on 2026-09-17 (`flush-on-shutdown`, docstore compaction ignoring
SIGTERM). A `prepareRestart` before stopping made nodes slower to return (16-28 min vs 9-11), per deploy README.

Why replay is 14.6M ops: `flush.memory.maxtlssize` is the default 20 GiB, and production feeds 26M ops/day per
node (serial deltas, 26 days, 3 nodes: 26.1-26.6M), so the tail is about 13 h of feed. The low-water mark at
the restart was set by `content_hash` and `path`, whose flushes rewrite the whole attribute (12 GB in 864 s and
29 GB in 474 s).

### 2.2 Write amplification

30 days, 12 nodes, `flush.complete` output sizes:

| target | GB/day per node (median) | note |
|---|---|---|
| memory index flush (new index data) | 32.5 | the only "new" bytes |
| disk index fusion | 4,400 | 136x the flushed bytes (130-145x per node); fusion busy 13-16 h/day |
| attribute flushes (whole-attribute rewrites) | 273 | about 8 GB/day would be the changed rows |
| document meta store flushes | 2,900 | includes a storm 2026-10-28..11-03: ~1,100 flushes/day of a 19 GB file. Cause not investigated here |

## 3. Merge policies on the production flush trace (H3)

`tools/mergesim.py` replays each node's 30-day flush trace (1,003 flushes, ~1 GB each on content-1) through
merge policies. One merge worker; merge throughput calibrated from the node's fusions (88 MB/s written);
base growth calibrated from the fused index (788 -> 1,090 GB). "Vespa current" fuses base + all flushed when
`trigger` flushed indexes exist; `trigger` is fitted per node so the simulated WA matches the measured one
(5-6), which also reproduces the measured fusion busy time (13.8-16.0 h/day vs 13-16 measured).

| policy | WA (12 nodes) | mean searched sources | fusion busy h/day | notes |
|---|---|---|---|---|
| Vespa today (measured) | 130-145x | 8.7-9.1 (sim), 11.07 measured in Sept | 13-16 | rewrites the 1 TB fused index ~5x/day |
| **tiered, fanout 4, full fusion at 25 % of base or 200 relative ids** | **6.5-7.5x** | **7.6-7.9** | **0.6** | the prototype's policy |
| Lucene TieredMergePolicy-like (size-tiered, fanout 10) | 2.5-3.2x | 15.7-17.3 | 0.2 | twice the sources; per-source query cost is why Vespa can't copy it |
| RocksDB universal / size-tiered fanout 4 | 3.5-3.8x | 9.0-10.1 | 0.3 | more sources than today |

H3 passes on all 12 nodes: tiered WA <= 15x and mean sources below today's calibrated policy. RocksDB
leveled compaction (WA ~ fanout x levels ~ 10-30x) needs key-range-partitioned levels, which an inverted
index with a global docid space doesn't have without a term-range split of every level; not simulated.

## 4. Tiered fusion prototype (branch `serpapi-8.781.11-tiered-fusion`)

TODO: e2e numbers (H5).

Design, as built (off unless `VESPA_TIERED_FUSION=1`):

- The fusion flush target runs either an intermediate merge or a full fusion. Policy: flushed indexes are bucketed
  by size tier (`floor` 64 MiB, factor `fanout` 4); when a tier holds `fanout` indexes they are merged into one.
  Full fusion when the unfused indexes reach 25 % of the fused index, or the newest id is 200 past the last
  fusion id (the source selector holds 254 sources).
- An intermediate over flush ids S takes the id max(S). The merge is the existing `Fusion`: documents whose
  selector value names no input are already dropped by `Fusion`, so nothing in the merge kernel changed.
  The newest flushed selector is used, so documents re-fed into a newer flush are dropped too.
- Commit: build in `index.inter.<id>` with a `merged_ids` file and `serial.dat` last; rename
  `index.flush.<id>` to `index.dead.<id>` and `index.inter.<id>` to `index.flush.<id>`; then delete the dead and
  merged-away directories (open files keep serving the old collection). Restart completes a merge interrupted
  after the commit point, drops one interrupted before it, and remaps selectors saved before the merge.
- In memory: the selector is cloned (O(N) memcpy with the FX-D patch, 41 ms at 590M lids) and remapped in one
  pass, as `doneFusion` clones it, so running queries keep a consistent selector/collection pair.
- Unit tests: merge contents (stale copy dropped), selector values, disk layout, restart, both crash windows,
  and a full fusion after an intermediate one.

Known gaps: no proton.def plumbing (environment variables), no metrics for intermediate merges, the
deleted-while-open old files rely on Linux unlink semantics, and the full fusion still rewrites the base
(the ~1/day full merge is the remaining 6x).

## 5. Restart on the blueprint node (i8g.4xlarge)

TODO.

## 7. Blueprint seams on current master: fork or not

Reference: BLUEPRINT.md (2026-11-05) "The 4 Exact Vespa 9 Surgical Patches" (~735 LOC). Prior spikes at v8.781.11:
`runs/seams-2026-11-06/seam{1..4}.md` (search-api-bench). Re-checked on master `b74dc9e9d40` (270 commits later):
no change under `vespalib/datastore/entryref.h`, `proton/documentmetastore`, `proton/reference`,
`proton/server/feedhandler.cpp`, `storage/persistence`; only +2 lines under `searchlib/tensor`; the update path
changed (`storeonlyfeedview.cpp` +44/-8: field-path updates now reindex indexed fields) without affecting the
attribute fast path. So the spikes' verdicts hold; LOC below are measured where marked, estimated otherwise.

| seam / part | blueprint LOC | verdict | LOC on master | basis |
|---|---|---|---|---|
| 1 lid space: 64 topical shards | ~80 | **no fork**: 64 doc types or content clusters + restricting Searcher (R5 pass: +1.4 % RAM, all-types queries 9x slower, so always restrict; summary cache ~10/64) | 150-300 Java + config (est.) | R5 |
| 1 widen lid/`EntryRef` past 2.1B | (same) | **not needed and not ~80 LOC**: lid is `uint32_t` (4.29B ceiling, not 2.1B); RAM per doc caps a node at ~1.2B docs first. Widening is a fork of 2,741 `uint32_t` docid/lid declarations in 875 files (searchlib 2,172/668, searchcore 479/162, searchsummary 90/45, measured `git grep -P` on master) plus 321 files using `EntryRef` (ABI break) and on-disk formats | 5k-15k changed (est.) | measured grep |
| 2 IVF + RaBitQ, contiguous lists | ~350 | **fork** (layout): stock IVF passes recall at 135 ns/code (R4), FastScan on contiguous lists 3-6 ns/code, 0.566 ms per 10M shard on i8g (R10) | prototype 1,500-2,200, production 4,400-6,700 (est.) | `seam2-fork-scope.md` (notes folder) |
| 3.1 direct-shard >= 100k fast-access updates/s | part of ~155 | **stock path insufficient** (R6: p99 drift < 5 % only to ~17k/s via messagebus). Bypassing distributor/persistence is a storage fork (1-3k LOC, loses bucket consistency). Cheaper first: core pinning of feed vs search threads, query not reading updated fields, batching (R6's fork-free reruns) | 0 (stock) / 1-3k fork (est.) | R6, seam3.md |
| 3.2 `uint64 linked_lids[3]` 1.5-hop | | **plugin, never lids**: lids move under lid-space compaction and bucket moves (`lid_space_compaction_job.cpp`, `bucketmovejob.cpp`); store 64-bit doc-id hashes (`array<long>`, `paged`) and fetch in a container Searcher; or stock `reference` + imported fields | 150-300 Java (est.) | seam3.md |
| 3.3 cluster-imputed priors `uint16`/`uint8` | | **stock**: two `fast-access` int attributes fed by partial updates (rides 3.1); or a rank-time constant tensor keyed by cluster | 30-60 feeder (est.) | seam3.md |
| 3.4 hydrate a node from S3 `.vespa-segment` < 12 min | | **same problem as fast restart** (this report): a node's files are keyed to its own lids, so "hydration" = copy the node's own snapshot + bounded replay. With §6 (bounded TLS tail, delta attribute checkpoints, tiered fusion so the big index changes ~1/day), an EBS-snapshot or S3 copy of the data dir restores to within the checkpoint interval; the remaining wall is bytes/s (prod ~4.3 TB per node). A remote loader for foreign lids is a 3-5k LOC fork and not recommended | 0 extra beyond §6 | C28 (< 12 min refuted at 3.75-5.6 TB), §6 |
| 4 renderer, micro-decomposer, caller/intent adapter, twiddlers, zstd serializer | ~150 | **no fork**: all container plugins (Searchers, a byte-level `Renderer`, `OnnxEvaluator`); "C++ set twiddlers" must run in the container where the merged top-N exists, so they are Java (prior art `ResultProcessingSearcher` host crowding); span selector moved to an external GPU service (R7, L40S 1.19 ms) | 1,500-2,600 Java (est.) | seam4.md, R7 |

### Test plans

- **Seam 2 (fork)**: unit tests mirroring `searchlib/src/tests/tensor/hnsw_index` (add/remove/lid reuse, `find_top_k`
  with and without global filter), a scalar-vs-SIMD kernel equivalence test, save/load and format-mismatch tests
  (a loader throw makes proton start the attribute empty, `tensor_attribute_loader.cpp:358`, so mismatches must
  fail `can_use_index_save_file` and rebuild), an ASan/TSan feed-while-query stress test (re-pack under
  `GenerationHolder`), then R10's recall/latency harness on the 10M slice against flat and stock IVF.
  Prototype selection without config-model: schema sentinel `hnsw { max-links-per-node: 1 }` dispatched in
  `DefaultNearestNeighborIndexFactory::make` (`default_nearest_neighbor_index_factory.cpp:28`).
- **Seam 3.1**: R6's step harness re-run with core pinning and a query that doesn't read the updated fields, then
  (only if still < 100k) a direct-RPC spike on one node with a consistency checker comparing attribute values to
  the document store after the run.
- **Seam 3.4 / restart**: the §5 restart harness (kill at the worst point of a steady feed, break down by phase)
  plus a restore-from-snapshot timing on the blueprint node.

## 6. Design: restart fast by construction

The goal is the database rule: recovery work is bounded by configuration, not by history, and nothing special has
to happen at shutdown (crash-only). PostgreSQL bounds WAL replay with `max_wal_size`/`checkpoint_timeout`;
RocksDB bounds it with `max_total_wal_size` and memtable flushes; Lucene's commit points make a crash lose at
most the uncommitted segment. In all three a checkpoint costs O(changes since the last one), which is what makes
frequent checkpoints affordable. Proton has a bound too (`maxtlssize`, 20 GiB, ~13 h of production feed), but
two of its checkpoint kinds cost O(total), so the bound is set high and the replay tail is huge.

Restart time = stop + component load + replay. Per component, today and by design:

| component | today (production) | cost model today | design | expected |
|---|---|---|---|---|
| stop | 1.5-7 min (`flush-on-shutdown`, compaction ignoring SIGTERM) | flush everything | crash-only: `flush-on-shutdown` off, SIGKILL is a normal stop | seconds |
| memory index checkpoint | 1 GB flush every ~45 min; each flush feeds the 136x fusion | O(delta) flush, O(total) fusion | **tiered fusion** (branch `serpapi-8.781.11-tiered-fusion`): small flushes are cheap to absorb, so flush on a short timer | fusion WA 6.5-7.5x (sim), flush cadence free |
| attribute checkpoint | whole attribute rewritten (`path` 29 GB/474 s, `content_hash` 12 GB/864 s), 1-4x/day | O(total) | **delta checkpoints**: write the changed rows since the last checkpoint (lid, value) every minute; merge deltas into a new full snapshot only when they reach a fraction of it (the same tiering) | ~8 GB/day instead of ~273 GB/day per node; checkpoint interval ~60 s |
| document meta store checkpoint | 17-19 GB rewritten per flush | O(total) | same delta scheme (gid -> lid/timestamp/bucket rows) | as above |
| document store | log-structured already | O(delta) | unchanged | - |
| TLS replay | 14.6M ops, 1,262 s | tail = 20 GiB of feed | tail = 60 s of feed at most; replay with parallel decode (branch `serpapi-8.781.11-fast-replay`) | prod: 60 s x 305 op/s = 18k ops, ~2 s |
| component load | 320 s (doc meta store 97 s, string/multi-value attributes 166-216 s, docstore 113 s) | read and rebuild every structure | **load = map** for fixed-width columns (single-value numeric attributes, doc meta store arrays): the file layout is the memory layout, `mmap(MAP_PRIVATE)`, faulted in on demand; enum/multi-value attributes keep the rebuild but load in parallel with replay of the fields that don't need them | TODO from local per-attribute load times |

Two things this design deliberately does not do:

- No `prepareRestart` special path. Restart cost is low because the steady state keeps it low, not because the
  node did extra work before stopping (which also failed in production: 10-min client timeouts, 16-28 min
  outages).
- No bigger RAM or "warmup" knobs: the fused index stays mmapped; the tiered fusion keeps the big index's page
  cache warm because the big index is rewritten ~1/day instead of ~5/day, and the intermediate indexes are small
  and recently written. The `search.mmap.advise=SEQUENTIAL` default (no LRU protection on Linux 6.1+, see
  `ISSUE_DRAFT.md`) should be `NORMAL` for posting files.
