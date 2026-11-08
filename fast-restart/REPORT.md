# REPORT: Vespa fork on 8.781.11, and a content node that restarts fast without fighting fusion

Preregistration: `PREREG.md` (commit f0934d5e4c1, written before any local run). Raw outputs (restart JSON,
perf data, query CSVs, vespa.log) are in `s3://si-greenfield-bench-524519922011/vespa-fork-restart/runs/`, binaries in
`.../artifacts/`. AWS: one c8gd.16xlarge compile box (~5.2 h) and one i8g.4xlarge measurement node (~8.7 h), both
spot in the greenfield-bench VPC, terminated and verified; ~$8.

## Summary

| question | answer | evidence |
|---|---|---|
| Fork on latest release | `serpapi-8.781.11` (5ecbbf9a1c): 4 patch sets + `-DNDEBUG` fix; all tests pass | §1 |
| Does the fork help in production? | content-1 serves the same queries at 0.83x matching latency and 0.84x query CPU of the stock nodes | `evals-internal .../runs/fork-ab-content1/REPORT.md` |
| Where does prod restart time go? | 1,585 s = 320 s load + 1,262 s replay of a 20 GiB (13 h of feed) TLS tail | §2.1 |
| Why is replay slow? | string-attribute dictionary maintenance with case-folded compare, not decode or I/O | §5 (perf) |
| Fusion WA, today vs tiered | prod 136x; simulated tiered 6.5-7.5x on all 12 nodes; measured locally 9.9-11.4x -> 4.3-4.5x with no lost docs and p50/p99 latency 0.78x/0.83x | §3, §4 |
| Cheapest restart wins | `<initialize><threads>` = cores (37 -> 17 s here); `match: cased` on `content_hash`/`path`; crash-only stop | §5 |
| Restart by design | bounded TLS tail via O(delta) checkpoints for every target: tiered fusion (prototyped), delta attribute/meta-store checkpoints (designed), load-by-map | §6 |

Hypotheses: H1 pass; H2 half falsified (not I/O bound, but not decode bound either); H3 pass (sim); H4 **not
tested as preregistered** (no prototype bounds the attribute checkpoints yet, so no "restart <= load + 60 s at the
worst point" run); H5 fails the 5x bound at this node's scale (2.5-3.2x), passes correctness.

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

### End-to-end on the blueprint node (H5)

Same i8g node and 50M-doc snapshot as §5. Each arm restores the snapshot, starts the build, deploys
`harness/app-ab` (identical in both arms: memory index flushed at 256 MiB so that, with a ~19 GB fused index,
fusion runs back to back and a full fusion is ~20+ flushes apart, as on a production node), then re-puts 30M
documents in 2M chunks at fixed offsets (identical content: documents are a function of their id). Two runs per
arm; the second ("-q") with a 50 QPS open-loop query load (R6's `r6` profile, seed 7) during the whole feed.
Stock = `vespaengine/vespa:8.781.11`; tiered = the `proto` build (tiered fusion + parallel replay code, only
`VESPA_TIERED_FUSION=1` set; verified in proton's `/proc/<pid>/environ`), fanout 4, full fusion at 25 % of the
fused index or 200 relative ids.

| arm | memory index flushed GB (flushes) | full / intermediate merges | fusion + intermediate bytes written GB | index WA | proton `write_bytes` GB | disk indexes searched, mean / max |
|---|---|---|---|---|---|---|
| stock | 18.6 (133) | 8 / 0 | 165.7 | 9.9x | 294.1 | 25.6 / 38 |
| stock -q | 19.8 (156) | 10 / 0 | 207.1 | 11.4x | 342.9 | 24.9 / 36 |
| **tiered** | 18.4 (130) | 2 / 37 | **65.0** | **4.5x** | **193.0** | **11.3 / 29** |
| **tiered -q** | 20.0 (159) | 2 / 48 | **65.7** | **4.3x** | **200.1** | **10.8 / 30** |

Per flushed GB, tiered wrote 2.5-3.2x fewer fusion bytes (index WA 4.3-4.5x vs 9.9-11.4x, n = 2 each), and
33-42 % fewer bytes overall from proton (document store and attribute writes are the same in both arms). Mean
searched sources fell from ~25 to ~11.

Query latency during the feed, 50 QPS open loop, same query sequence (block bootstrap over 60 s blocks):

| percentile | stock ms | tiered ms | tiered / stock | 95 % CI |
|---|---|---|---|---|
| p50 | 124.0 | 96.4 | **0.778** | 0.747-0.809 |
| p95 | 270.2 | 216.5 | **0.801** | 0.767-0.834 |
| p99 | 344.2 | 286.7 | **0.833** | 0.793-0.866 |

Correctness: total hits (rank profile `unranked`) for 10 fixed queries, including match-all = 50,000,000, are
identical between the stock and tiered arms after the feed (all 10 equal in both pairs).

H5 verdict: **fails the preregistered 5x bound** (2.5-3.2x), passes the no-lost-documents condition. The bound
was set from production's 136x; on this node the fused index is only ~19 GB, so stock's own WA is ~10x, and the
25 %-of-base rule forces a full fusion every ~25 min (2 per arm). The production-scale effect is the simulation's
(§3: 136x -> 6.5-7.5x); the local run confirms the direction, the source-count drop and the latency gain, and that
the mechanism keeps every document.

Open defects found by this run:
- 2 of 190,326 queries in the tiered -q arm hit the 2 s timeout (504), both scheduled at the exact instants the two
  full fusions completed (t = 1,943.4 s and 3,415.0 s); stock had 0 of 207,926 across 10 full fusions. Cause not
  found: the full-fusion path is stock code, but in the tiered arm it starts from a source list that includes
  intermediate indexes. Must be explained before any canary.
- 9 "same target webpage.0.ready.summary.flush has been asked to flush again" warnings in the tiered -q arm (0 in
  the other three arms). Document store target, not index; not investigated.

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

Node: i8g.4xlarge spot (16 vCPU Graviton4, 128 GiB, 3.75 TB NVMe), Vespa 8.781.11 in Docker, deployed `webpage`
schema with production proton overrides (`harness/app`), 50M synthetic documents fed in 4,969 s (10.1k docs/s,
0 errors). On disk after load: fused index ~19 GB (prod: ~1.06 TB at 465M docs, so index bytes per doc are ~6x
smaller than production's), document store ~38 GB. Every restart starts from the same reflink snapshot arm;
`restart.sh` triggers a flush, re-puts N random existing documents, then SIGKILLs proton (or SIGTERMs it through the
sentinel for `clean`). Times are from proton's own event log.

| run | n | ops replayed | replay s | replay op/s | init s | SIGKILL -> `started` s |
|---|---|---|---|---|---|---|
| stock, kill, no tail | 2 | 13-58k | 0.1-0.4 | - | 35.1 / 36.7 | **36.2 / 37.5** |
| stock, kill after 5M re-puts | 3 | 5.01-5.06M | 41.3 / 42.6 / 42.1 | 122.6k / 117.9k / 119.2k | 35.8-37.3 | **82.8 / 85.3 / 85.4** |
| stock, clean stop (`flush-on-shutdown`) after 2M | 1 | 2.07M | 26.4 | 78.5k | 36.8 | 66.1 (+ 4.9 s stop) |
| parallel decode, 8 threads, kill after 5M | 3 | 5.02-5.06M | 40.5 / 38.8 / 36.8 | 125.0k / 129.6k / 137.2k | 36.0-37.2 | **82.2 / 81.3 / 80.1** |
| parallel decode, kill, no tail | 1 | 68k | 0.4 | - | 37.0 | 38.0 (init to started; see sentinel note) |
| initializer threads 16 (config only), kill, no tail | 3 | 58k | 0.4 | - | 16.0 / 15.8 / 15.9 | **17.2 / 17.0** (3rd: 104.9, sentinel back-off 88 s; init 15.9) |
| initializer threads 16, kill after 5M | 1 | 5.06M | 37.8 | 133.9k | 16.8 | **60.3** |

Findings:

- **H1 (replay > 50 % of restart with >= 5M ops): pass.** 42 s of 85 s (49-50 %) at 5M ops; replay grows
  linearly with the tail, so it dominates past ~5M ops. Production's 14.6M-op tail at its 11.5k op/s is 80 %.
- **H2: half falsified.** Replay is not I/O bound (NVMe read 60 MB/s average during replay, << device), but the top
  of the `perf` profile is *not* decode: attribute field writers take 54 % of proton CPU, 36.6 % in
  `AttributeVector::commit`, 25 % in string enum dictionary lookups (`EnumStoreT<const char*>::find_index`,
  B-tree `lower_bound`) with case-folding comparison (`FoldedStringCompare::compareFolded`, 20 % self). Document
  store zstd compression is 11 %; the master thread's decode + dispatch is ~13 %.
  - This explains production's 10x slower replay (11.5k vs ~120k op/s here): the attributes that lagged at the
    production restart, `content_hash` and `path`, are plain `string` attributes with default (uncased) matching,
    so every replayed value is inserted into a dictionary of hundreds of millions of strings with a case-folding
    comparator. Locally the synthetic dictionaries are small.
  - Cheapest production fix (config, not code): `match: cased` on `content_hash` (a hash) and `path` (URL paths are
    case-sensitive); add `dictionary: hash` where only exact match is needed. Not measured here (needs a
    schema change and refeed of those attributes).
- **Parallel decode prototype: +8.7 % replay throughput** (median 129.6k vs 119.2k op/s, ranges disjoint), -4 s on a
  85 s restart. Amdahl-limited exactly as the profile predicts. Kept as a branch, not a recommendation.
- **"Clean" stop doesn't bound replay**: with `flush-on-shutdown` the prepare-restart strategy weighed flush cost
  against estimated replay cost and left a 2.07M-op tail; restart was 66 s. Consistent with production, where
  `prepareRestart` before stopping made outages longer.
- **Init is thread-starved, not I/O-starved.** `Start initializing components: threads=2`: the default pool is
  (document databases + 1) threads (`ContentSearchCluster.java:277`), not a function of cores: 2 here, 7 in
  production on 64 vCPU. Attributes load two at a time (~4 s each, a 20 s staircase), and the document store
  (9.2 s) waits behind them. The components' own times sum to the init time; the longest single chain is ~17 s.
  **With `<initialize><threads>16</threads></initialize>` (one line in services.xml) init drops from 35-37 s to
  15.8-16.8 s and a no-tail restart from 36-38 s to 17 s; the 5M-op restart from 85 s to 60 s.** Production runs
  7 threads on 64 vCPU for 6 document databases; its init was 320 s with single attributes taking up to 216 s,
  so there the gain is bounded by the slowest attribute (estimated 320 -> ~220-250 s, not measured).
- **Sentinel back-off is part of restart time in a crash-only design.** Each SIGKILL doubles the config sentinel's
  restart penalty (2, 6, 14, 30, ... 510 s, decaying slowly); the 7th kill in one container waited 228 s before
  proton started. Production restarts the container through systemd, which resets it, but an in-place crash-only
  restart policy must not escalate on deliberate kills.

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
| component load | 320 s (doc meta store 97 s, string/multi-value attributes 166-216 s, docstore 113 s) | read and rebuild every structure | **load = map** for fixed-width columns (single-value numeric attributes, doc meta store arrays): the file layout is the memory layout, `mmap(MAP_PRIVATE)`, faulted in on demand; enum/multi-value attributes keep the rebuild but load in parallel with replay of the fields that don't need them | locally (50M docs): single-value numeric attributes load in 0.26-0.30 s each, string/multi-value 3.6-4.9 s, doc meta store 7.4 s, document store 7.6-9.9 s; with 16 initializer threads init is 16 s, the doc meta store + document store chain. Map-loading the numeric columns saves little; the doc meta store and string attributes are where load = map pays |

Two things this design deliberately does not do:

- No `prepareRestart` special path. Restart cost is low because the steady state keeps it low, not because the
  node did extra work before stopping (which also failed in production: 10-min client timeouts, 16-28 min
  outages).
- No bigger RAM or "warmup" knobs: the fused index stays mmapped; the tiered fusion keeps the big index's page
  cache warm because the big index is rewritten ~1/day instead of ~5/day, and the intermediate indexes are small
  and recently written. The `search.mmap.advise=SEQUENTIAL` default (no LRU protection on Linux 6.1+, see
  `ISSUE_DRAFT.md`) should be `NORMAL` for posting files.

## 8. Follow-up (2026-11-08): the full-fusion stall, delta checkpoints and H4

New branches on `serpapi/vespa`, each on `serpapi-8.781.11`:

| branch | what | tests |
|---|---|---|
| `serpapi-8.781.11-async-index-removal` | old disk indexes removed by a per-index-maintainer remover thread, never by the thread dropping the last reference | new `indexmanager_test` case; the 4 index test programs pass; 0 failures in 40 sequential runs |
| `serpapi-8.781.11-delta-checkpoints` | delta checkpoints for plain single-value numeric attributes and the document meta store (`VESPA_DELTA_ATTRIBUTE_CHECKPOINTS=1`) | new `searchlib_delta_checkpoint_test_app` (7 cases), 3 new document meta store cases (57/57 pass); attribute, attribute flush and initializer suites pass |
| `serpapi-8.781.11-fast-restart-h4` | test build only: tiered fusion + the two above (merges) | the above plus the tiered fusion tests: 0 failures in 60 runs |

### 8.1 Why tiered fusion timed out 2 queries at full fusion completion

Cause: `DiskIndexWithDestructorCleanup`'s destructor runs `DiskIndexCleaner::removeOldIndexes()`, an `rm -rf` of
every old index directory, on whichever thread drops the last reference to an old index. After a full fusion that
is usually a query that was running on the old index collection when `doneFusion` swapped in the new one: the
query deletes the old fused index and the merged inputs before its reply goes out. Stock code, not tiered code.

Evidence (the first A/B's raw data, `runs/fast-restart-out.tgz`):

| arm | queries | full fusions | queries > 700 ms | of those, within 1 s after a full fusion completed | slowest | deleted per full fusion |
|---|---|---|---|---|---|---|
| stock -q | 207,926 | 8 (in log retention) | 8 | **8** | 1.16-1.67 s | 20.7 GB fused + 2.0-2.3 GB flushed |
| tiered -q | 190,326 | 2 | 2 | **2** | 2.0 s (timeout) | 20.7 GB fused + 5.2 GB flushed/intermediate |

So stock 8.781.11 also stalls one query per full fusion, just under the 2 s timeout; tiered fusion deletes ~3 GB
more of recently written (page-cached) data per full fusion and crosses it. The stalled stock query is the one sent
~0.7 s after `fusion.complete`, which is when stock's `doneFusion` finishes its 50M-lid selector clone and swaps;
with the FX-D clone in the fork the swap is immediate and the stalled tiered queries were sent 4 ms and 55 ms
before `fusion.complete`. Every other query in both arms stayed under 700 ms.

Fix (`serpapi-8.781.11-async-index-removal`): each `IndexMaintainer` owns a one-thread index remover; disk index
wrappers schedule the cleanup there (still under the remove lock), and the remover drains at shutdown. The unit
test blocks the remover, drops the last reference from the test thread and checks the directory is still there
until the remover runs. Production relevance (not measured): a production full fusion deletes a ~930 GB fused
index; whichever query holds the last reference waits for that.

The 9 "`webpage.0.ready.summary.flush` has been asked to flush again" warnings (only in tiered -q, at t = 785,
949 and 1,995 s, none at the timeouts): the document store's memory gain is `WriteableFileChunk::getMemoryFootprint`,
which counts chunks already queued for compression and write. A flush can't free those, so while the writer lags
(median summary memory 1.18 GB in tiered -q vs 0.86 GB in stock -q; 137 vs 49 summary flushes) the memory
strategy picks the same target again and the flush engine logs the warning and waits 100 ms. Each such flush took
0-8 ms. Benign, stock behaviour; a fix would subtract queued chunks from the gain. Not done here.

### 8.2 INTERIM (2026-11-08 14:55Z): the stall fix on the i8g run

**INTERIM.** One arm is final and one is partial; the rest of the run is still going. Same i8g.4xlarge node
(`i-0f1da773f3ac64eef`), the same 50M snapshot restored for each arm, and the same app-ab with 50 qps of open-loop
queries while 30M documents are re-put. Latency is measured from the scheduled send time; the timeout is 2 s.

| arm | status | feed, min | queries | p50 | p99 | max | timeouts/errors | queries > 700 ms | of those, within 1 s after a fusion completed | fusions completed | proton write_bytes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ab-stock-q2 (stock 8.781.11) | final | 67 | 200,400 | 126 ms | 358 ms | 1,816 ms | 0 | 8 | **8** (all +0.70-0.76 s) | 8 | 342.7 GB (11.4 KB per re-put) |
| ab-fix-q (stock + async index removal) | partial, 59 of ~67 min | 59 | 173,793 | 98 ms | 276 ms | **659 ms** | 0 | **0** | 0 | 7 | pending |

This reproduces §8.1 on a fresh node: stock stalls exactly one query per full fusion, each sent ~0.73 s after
`fusion.complete`, at 0.9-1.8 s. With the fix, no query reached 700 ms across 7 full fusions.

Still to come:
- ab-fix-q final, around 15:10Z;
- ab-fixtiered-q (tiered fusion plus the fix), around 16:25Z;
- the three H4 arms (h4-stock, h4-stock-tls1g, h4-proto), with restart time and flush+fusion bytes by group, around 20:30Z.

The H4 arms run one after another on this node. Running them in parallel on extra nodes was not approved in this
session, and the H4 preregistration requires at least 50 minutes of feed per arm. H4 write amplification is
therefore not measured yet.
