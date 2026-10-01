# Cheaper extra index generations at query time, and why fusion is decode-bound

Branch `agents/disk-index-fusion-optimization` on vespa master `b135ebe18c7`. All numbers were measured on
on-demand c8a.16xlarge boxes (AMD EPYC 9R45, the same Zen 5 CPU as the production r8a nodes; 64 cores, no SMT,
1 MiB L2 per core, 32 MiB L3 per CCD; Amazon Linux 2023, kernel `6.1.188-233.386.amzn2023`, MGLRU off): one for
the main work and a second one to verify both patch branches together. Both boxes were torn down.

Short version:

- Task 1: the strict source blender no longer scans every source per candidate. At 11 sources (the production
  time-weighted mean) a returned hit costs 8.84 instead of 16.43 ns without unpack and 31.54 instead of 42.19 ns
  with unpack (460M docid space, 5.9M postings, 2% stale). An extra source used to cost 0.59 ns per hit, it now
  costs 0.02 ns. k=1 also got faster (-22% / -16%). Results are identical to the old implementation on 1000
  randomized setups plus every benchmark configuration. For a query like "apple" (5.9M hits/node) that is about
  45-63 ms less CPU per query per node.
- Task 2: the 66.6% "self time" of `Zc4PostingReader<true>::read_doc_id_and_features` is a sampling artifact. 92%
  of that function's cycle samples and 94% of its retired-instruction samples land on one 8-byte store that runs
  once per posting. When the store-forwarding conflict it waits on is removed, total cycles don't move and the
  samples jump into `readFeatures`. Ablations on 14 GB of Common Crawl text show what fusion actually spends per
  posting: 333 cycles, of which 58 are positions (19 cycles each), 39 are interleaved features, and about 236 are
  per-posting machinery. A cheaper raw copy of positions buys at most 1.2x per core, replacing the byte-at-a-time
  writer gives about 1.1x, and a word-range-parallel merge scales almost linearly (24 streams: 22x, 3.8 GB/s).
- Task 3: issue draft in [ISSUE_DRAFT.md](ISSUE_DRAFT.md). I checked the MADV_SEQUENTIAL claim against Linux 6.1,
  6.1.188 stable and the Amazon Linux 6.1.188 sources, then reproduced it on the same kernel.

## Commits and verification on both bases

| branch | base | tensorFromWeightedSet patch | strict blender patch |
|---|---|---|---|
| `agents/disk-index-fusion-optimization` (this work) | master `b135ebe18c7` | none | `fee9b7b9a86`, with this report as the commit on top |
| `agents/serpapi-vespa-perf` (upstream base) | master `b135ebe18c7` | `fd3f64f60a0` | `39baddc7e0a` |
| `agents/serpapi-8.679.50` (prod base) | tag `v8.679.50` `a102426624b` | `bd5257a29c0` | `801cfcd4314` |

- `39baddc7e0a` is a cherry-pick of `fee9b7b9a86` with the same patch-id. The two patches touch disjoint files
  (`searchlib/.../features` and `searchlib/.../queryeval`), so it applied cleanly.
- `801cfcd4314` is the backport. Only context lines in `sourceblendersearch.cpp` and `sourceblender_test.cpp`
  conflicted, because master was clang-formatted after 8.679.50. The resolved files are token-identical to the
  master versions, ignoring whitespace, braces, comments and include/using order. The other four files are
  byte-identical. Its message follows the tensor backport's convention.
- The report commit is on neither branch. The fusion work (Task 2) is profiling only, so there is no fusion
  commit; its tool lives in `report-artifacts/`.

Both branches were verified together on a second c8a.16xlarge with the same CPU, kernel and build image digest as
below (20 minutes, then torn down). Each branch was shipped as a git bundle and checked out at exactly the hashes
above. Targets and tests were taken from each tree's own CMakeLists (`report-artifacts/scripts/both-bases/`).

| | `agents/serpapi-vespa-perf` `39baddc7e0a` | `agents/serpapi-8.679.50` `801cfcd4314` |
|---|---|---|
| build: every executable under `searchlib/src/tests/{features,queryeval,diskindex}`, `vespa-index-inspect`, 6 searchcore test apps | 90 targets, 0 errors | 86 targets, 0 errors |
| ctest: every non-benchmark test in those directories, plus searchcore query, querynodes, matching, indexmanager, fusionrunner, indexcollection | 80/80 pass | 76/76 pass |
| sourceblender benchmark: old vs new hits and checksum, 76 configurations | no mismatch | no mismatch |

- Master has 4 feature tests that 8.679.50 doesn't (first_phase_max, matches_for_labels,
  query_term_document_frequency, tensor_from_labels_with_offset), which accounts for 80 vs 76.
- The tensorFromWeightedSet and tensorFromLabels tests from the other patch ran and passed on both bases.
- The 8.679.50 tree compiles with the September build image (GCC 15.2.1). No warnings come from files either patch
  touches. Both trees print bison's shift/reduce note for `document/select/grammar/parser.yy`, and 8.679.50 also
  prints 3 `-Winline` notes from `eval/aggr.h`.

Strict blender at production density on both bases. ns per returned hit, 460M docids, 5.9M postings, 2% stale,
min of 5 runs:

| sources | unpack | `serpapi-vespa-perf` | `serpapi-8.679.50` |
|---|---|---|---|
| 2 | no | 11.30 → 8.72 | 11.11 → 8.51 |
| 2 | yes | 37.17 → 31.26 | 36.48 → 30.88 |
| 11 | no | 16.38 → 8.87 | 16.46 → 8.80 |
| 11 | yes | 42.58 → 31.64 | 41.95 → 31.43 |
| 16 | no | 19.69 → 9.07 | 20.32 → 9.37 |
| 16 | yes | 46.54 → 31.96 | 46.61 → 32.10 |

- These agree with the master-only runs in the tables below within 3.6% (median 0.4% and 1.1%).
- The 50M-docid runs agree to a median of 0.8% and 1.7%. Individual configurations differ by up to 16% between the
  two boxes, mostly faster on the second one, for both old and new code.
- On each base the new code was faster than the old in all 76 configurations, by at least 1.18×.

## Setup

| | |
|---|---|
| Build image | `vespaengine/vespa-build-almalinux-8@sha256:9bf771f1d3a3…` (GCC 15.2.1, CMake 3.30.9) |
| Flags | Vespa defaults: `-g -O3 -fno-omit-frame-pointer -march=haswell -mtune=skylake-avx512` |
| Pinning | microbenchmarks pinned to core 8 (`taskset -c 8`), nothing else running |
| perf | host perf 6.1 for the microbenchmark counters (`perf stat -C 8` with `--control fifo`, so only timed loops count); perf 4.18 in a privileged build container for the fusion counters and for `record`/`annotate` |

Production runs 8.679.50. I compared the relevant sources between `v8.679.50` and `HEAD` after stripping comments,
whitespace and braces. The strict blender files (`sourceblendersearch.{h,cpp}`, `isourceselector.h`,
`searchiterator.h`, `fixedsourceselector.cpp`) are identical. The fusion hot path (`zc4_posting_reader*.cpp`,
`compression.h`, `posocccompression.cpp`, `fieldreader.cpp`, `fieldwriter.cpp`, `zcposting.cpp`) and the Zc posting
iterators differ only in formatting, `using` order and split string literals. `singlenumericattribute.h`, which
stores the selector, renames `RcuVectorBase` to `TypeStableVectorBase` and adds `reserved_memory_for_flush()`.
`field_merger.cpp` has a FastOS to `std::filesystem` change and an extra `_word_num_mappings.clear()`, neither on
the hot path. The production `libvespa_searchlib.so` (from `vespaengine/vespa:8.679.50`, GCC 15.1.1) keeps
`.symtab` because the RPM strips only debug info, so production perf symbol names are trustworthy. Its
`read_doc_id_and_features` is 0x527 bytes, the same as in this build, and it contains the same instruction
sequence discussed in Task 2.

## Task 1: strict source blender

### What changed

`SourceBlenderSearchStrict::doSeek()` probed the selector for the target docid, then called `seek()` on every
child. `advance()` then walked all k+1 children through `_sources[_children[i]]` for every candidate docid,
collected the ones at the minimum into a vector, looked the minimum up in the selector, and compared pointers.
That costs O(k) pointer-chasing loads and branches per returned hit.

The new implementation (`searchlib/src/vespa/searchlib/queryeval/sourceblendersearch.cpp`):

- mirrors each child's current docid in a contiguous `std::vector<uint32_t>`, written only when the blender moves
  that child (resynced in `initRange()` and `transform_children()`);
- tracks the child at the smallest docid (`_min_idx`, `_min_child`) and the smallest docid of all other children
  (`_others_min`), recomputed by a branch-free scan only when the leading child passes the bound or children tie;
- fast path: while the leading child stays below `_others_min` (one big fused index plus small flushed ones), a hit
  costs one direct `doSeek` on that child, one selector lookup and one pointer compare against `_min_child`;
- calls `doSeek()` directly on children known to be behind the target, skipping the redundant `seek()` check;
- keeps one old corner case on purpose: seeking exactly to a hit beyond the end id still reports it, as the old
  probe-first code and the non-strict blender do.

An intermediate version (v1) looked owners up through a source-to-child table and read the cached docid. It was
already 43% faster at k=10 but no faster at k=1 with unpack (+0.3% cycles). The committed version (v2) compares
the selected child's pointer against the cached leading child and calls `doSeek()` directly, which leaves fewer
dependent loads in front of the unpack's indirect call. With it, k=1 with unpack is 16% faster than the old code.

### Benchmark

`searchlib/src/tests/queryeval/sourceblender/sourceblender_benchmark.cpp` (standalone app, not a ctest):

- one large child plus k small ones. Children are real disk indexes built with `diskindex::IndexBuilder`: Zc4
  postings with word positions and interleaved features (like `index: enable-bm25`), mmapped, iterators from
  `FieldIndex::create_iterator`. There is also an `array` child kind with near-zero child cost, which isolates
  the blender;
- source 0 is the big index, sources 1..k are newer. A fraction `stale` of the big child's postings belong to a
  random newer source (`FixedSourceSelector`). Up to half of each small child are such re-fed documents that still
  contain the term, so equal docids appear in several children. The rest are new documents. Positions are derived
  from (docid, source), so unpacking the wrong child changes the checksum;
- iteration copies `MatchThread::inner_match_loop`: `initRange`, `seekFirst`, `seekNext(docid + 1)`, optional
  `unpack`, with a checksum over docids and unpacked field length, occurrence count and first position;
- the pre-change algorithm is kept verbatim as `test::LegacyStrictSourceBlender`
  (`legacy_strict_source_blender.h`), so old and new run in the same binary on the same data in rotating order.
  Each configuration first runs both untimed and fails on any hit-count or checksum mismatch. None occurred in any
  run below.

Control: with the library still unchanged, the in-binary legacy copy and the library's own strict blender were
within 0.93-1.04x of each other across all 60 configurations (median 1.00), so the copy is a fair stand-in.

### Results: 50M docid space, 5M postings in the big child, 5k in each small child

Min ns per returned hit over 7 runs (medians within 5% of min). zc = Zc4 disk-index children.

zc, no unpack:

| sources (k+1) | stale 0% | stale 2% | stale 10% |
|---|---|---|---|
| 2 (k=1) | 9.36 → 7.65 (-18%) | 9.60 → 6.91 (-28%) | 11.03 → 8.15 (-26%) |
| 4 (k=3) | 10.23 → 6.69 (-35%) | 10.63 → 7.03 (-34%) | 12.14 → 8.27 (-32%) |
| 6 (k=5) | 11.29 → 6.73 (-40%) | 11.54 → 7.04 (-39%) | 13.17 → 8.44 (-36%) |
| 11 (k=10) | 13.99 → 6.96 (-50%) | 14.20 → 7.22 (-49%) | 15.96 → 9.62 (-40%) |
| 16 (k=15) | 16.76 → 7.24 (-57%) | 17.30 → 7.56 (-56%) | 19.16 → 8.84 (-54%) |

zc, with unpack:

| sources | stale 0% | stale 2% | stale 10% |
|---|---|---|---|
| 2 | 34.77 → 30.43 (-12%) | 35.36 → 29.46 (-17%) | 38.77 → 32.48 (-16%) |
| 4 | 35.53 → 28.74 (-19%) | 36.42 → 29.49 (-19%) | 39.72 → 32.59 (-18%) |
| 6 | 36.94 → 28.88 (-22%) | 37.31 → 29.68 (-20%) | 40.83 → 32.69 (-20%) |
| 11 | 39.61 → 29.15 (-26%) | 40.53 → 29.93 (-26%) | 44.00 → 34.55 (-21%) |
| 16 | 43.09 → 29.45 (-32%) | 44.11 → 30.28 (-31%) | 47.77 → 33.42 (-30%) |

array children (blender cost almost alone), no unpack / with unpack, stale 2%:

| sources | no unpack | with unpack |
|---|---|---|
| 2 | 7.70 → 5.49 (-29%) | 9.43 → 7.27 (-23%) |
| 4 | 8.69 → 5.37 (-38%) | 10.35 → 7.12 (-31%) |
| 6 | 10.19 → 5.42 (-47%) | 11.87 → 7.18 (-40%) |
| 11 | 14.32 → 5.65 (-61%) | 16.02 → 7.40 (-54%) |
| 16 | 19.48 → 6.01 (-69%) | 20.87 → 7.80 (-63%) |

### Results at production density: 460M docids, 5.9M postings ("apple" on one node)

zc children, min of 5 runs:

| sources | stale | no unpack | with unpack |
|---|---|---|---|
| 2 | 0% | 10.80 → 8.32 (-23%) | 36.51 → 30.81 (-16%) |
| 2 | 2% | 11.31 → 8.81 (-22%) | 37.07 → 31.22 (-16%) |
| 6 | 2% | 13.43 → 8.65 (-36%) | 39.03 → 31.39 (-20%) |
| 11 | 0% | 16.26 → 8.61 (-47%) | 41.50 → 30.96 (-25%) |
| 11 | 2% | 16.43 → 8.84 (-46%) | 42.19 → 31.54 (-25%) |
| 16 | 2% | 19.62 → 9.13 (-53%) | 46.15 → 31.90 (-31%) |

Marginal cost of one more source (2% stale, sources 2→16): old 0.59 ns per hit (0.65 with unpack), new 0.023
(0.049 with unpack).

No dominant child (every child has 500k postings, 50M docids, no unpack). The rescan runs on almost every hit here,
and it is still faster:

| sources | zc, stale 0 | zc, stale 10% | array, stale 0 |
|---|---|---|---|
| 2 | 14.32 → 11.88 (-17%) | 15.20 → 12.90 (-15%) | 12.65 → 10.17 (-20%) |
| 4 | 23.27 → 15.87 (-32%) | 19.43 → 17.64 (-9%) | 16.21 → 13.17 (-19%) |
| 11 | 30.15 → 23.31 (-23%) | 30.24 → 23.95 (-21%) | 24.63 → 18.63 (-24%) |
| 16 | 42.13 → 31.68 (-25%) | 42.72 → 31.84 (-25%) | 36.32 → 26.43 (-27%) |

### perf stat per returned hit (AMD Zen 5, zc children, stale 2%, 5 timed runs)

| docids | sources | unpack | impl | ns | cycles | instructions | IPC | branch-misses | L1D misses | L2 misses |
|---|---|---|---|---|---|---|---|---|---|---|
| 50M | 2 | no | old | 9.71 | 43.8 | 170.4 | 3.89 | 0.224 | 0.24 | 0.00 |
| 50M | 2 | no | new | 6.93 | 31.3 | 146.2 | 4.67 | 0.122 | 0.23 | 0.00 |
| 50M | 2 | yes | old | 35.67 | 160.7 | 494.4 | 3.08 | 1.740 | 0.32 | 0.01 |
| 50M | 2 | yes | new | 29.27 | 131.8 | 470.2 | 3.57 | 1.609 | 0.32 | 0.01 |
| 50M | 11 | no | old | 14.27 | 64.2 | 309.4 | 4.82 | 0.237 | 0.24 | 0.01 |
| 50M | 11 | no | new | 7.27 | 33.1 | 149.2 | 4.51 | 0.142 | 0.24 | 0.01 |
| 50M | 11 | yes | old | 40.02 | 180.4 | 633.3 | 3.51 | 1.844 | 0.35 | 0.01 |
| 50M | 11 | yes | new | 29.93 | 135.1 | 473.2 | 3.50 | 1.654 | 0.35 | 0.01 |
| 460M | 2 | no | old | 11.30 | 51.0 | 179.2 | 3.51 | 0.324 | 1.33 | 0.02 |
| 460M | 2 | no | new | 8.47 | 38.3 | 147.8 | 3.86 | 0.310 | 1.30 | 0.01 |
| 460M | 2 | yes | old | 37.05 | 167.2 | 503.1 | 3.01 | 1.843 | 1.43 | 0.03 |
| 460M | 2 | yes | new | 31.15 | 140.3 | 471.8 | 3.36 | 1.813 | 1.42 | 0.02 |
| 460M | 11 | no | old | 16.43 | 74.0 | 331.6 | 4.48 | 0.322 | 1.34 | 0.02 |
| 460M | 11 | no | new | 8.93 | 40.3 | 150.4 | 3.73 | 0.330 | 1.32 | 0.02 |
| 460M | 11 | yes | old | 42.62 | 191.8 | 655.5 | 3.42 | 1.946 | 1.48 | 0.03 |
| 460M | 11 | yes | new | 31.81 | 143.3 | 474.4 | 3.31 | 1.854 | 1.47 | 0.03 |

Events: `cycles`, `instructions`, `branch-misses` (0xc3 ex_ret_brn_misp). L1D misses are
`l2_cache_req_stat.dc_access_in_l2` (0x64/0xf8, L1D requests that reached L2, excluding L2 prefetch). L2 misses
are `l2_cache_req_stat.ls_rd_blk_c` (0x64/0x08, data requests that missed L2). Encodings come from the kernel's
`amdzen5` event tables.

The EC2 guest PMU returns 0 for the L3/DRAM fill events (`ls_dmnd_fills_from_sys.*`, `ls_any_fills_from_sys.*`)
and for the generic `L1-dcache-load-misses` and `cache-references` encodings, and it exposes 5 counters with no
IBS. So real LLC misses could not be counted. L2 misses stay at or below 0.03 per hit, which bounds them.

The old code was instruction-bound: high IPC, many cheap loads and branches per child. The new code does 55% fewer
instructions per hit at 11 sources, and its cost per hit barely changes with the number of sources. The 460M runs
show 1.3 L1D misses per hit (0.24 at 50M), most likely from the 460 MB selector array, the same for old and new. The L2 prefetchers cover the
increasing-docid access pattern.

### Correctness

- `sourceblender_test` now has 8 tests: the 3 existing ones plus exhausted children, equal docids across
  children, the selector pointing at a source without the doc or without a child, docid limit and end id (with the
  exact-seek-beyond-end corner case), and a randomized differential test. The randomized test covers 1000 setups,
  1-7 children with random source ids and densities, a mix of `SimpleSearch` children (which report hits beyond the
  end id) and `DocIdIterator` children (which pre-seek in `initRange`), 3 random ranges each including rewinds,
  and 200 random sequential, jumping and out-of-order seeks with random unpacks. Every seek is compared against
  the legacy implementation (`seek()` result and docid) and against a brute-force model of the selector and
  postings, and the per-child unpack logs are compared after each setup.
- The edge-case tests and the model passed against the old library before the change, so they describe the
  existing semantics rather than the new code.
- `ctest`: 32 searchlib queryeval tests plus `searchcore_{query,querynodes,matching,indexmanager,fusionrunner,
  indexcollection}_test_app`, 38 in total, all pass.
- The benchmark checks old against new (hits and checksum) in every configuration of every run above: 244
  configurations, no mismatch.

### What it means in production

Production profile of "apple" (5.9M matches per node, about 11 sources): strict blender 18% of query CPU. At 11
sources and production density the benchmark saves 7.6 ns per hit without unpack and 10.7 ns with unpack, so about
45-63 ms of CPU per query per node. Every flushed index waiting for fusion used to add 0.6 ns per hit to every
query term with many hits; now it adds about 0.02-0.05 ns. Two assumptions: production children are single-field
term iterators like the benchmark's, and per-hit ranking work outside the blender is unchanged. For multi-field
queries `optimize_source_blenders()` puts OR iterators under the blender, which gives the same per-source shape.

## Task 2: why disk index fusion looks decode-bound

### Data

- Corpus: Common Crawl `CC-MAIN-2026-34`, WET files `…-00000` to `…-00299` (the first 300 of 100,000 in
  `wet.paths.gz`), records with `WARC-Identified-Content-Language: eng` only, so a whitespace tokenizer stays
  honest. Tokens are lowercased ASCII alphanumerics plus any byte >= 0x80, max token length 1000, at most 10000
  occurrences per term (Vespa defaults).
- Schema: one string index field `body` with positions and interleaved features (what `index: enable-bm25`
  turns on), Zc4 posting format.
- Large index: 2,080,000 docs (14.3 GB of text, 2.21G tokens), built as 32 batch indexes (65k docs each, written by
  `IndexBuilder` as a memory-index flush would) and then fused, which is the same history as a production fused
  index. Result: 11,746,411 words, 726,378,042 postings (93.5% in words with at least 64 docs, i.e. the skip-list
  "common word" format), 7.48 GB `posocc.dat.compressed`, 8.54 GB in total.
- 8 small indexes: 5,000 docs each, 2,500 re-fed docids from the large range and 2,500 new ones, 13.6M postings in
  total.
- Measured fusion: large + 8 small, 740.0M input postings → 732.8M output postings (the 7.2M re-fed postings in the
  large index are dropped), 8.70 GB in, 8.61 GB out.

Tool: `report-artifacts/fusion_profile.cpp` (build and fuse subcommands). It calls `diskindex::Fusion` the way
`IndexManager::runFusion` does, with a 4-thread `ThreadStackExecutor` instead of proton's shared executor.

### Throughput

| run | wall s | CPU s | in MB/s (wall) | in MB/s per CPU-second |
|---|---|---|---|---|
| DIRECTIO read and write (proton default `indexing.{read,write}.io`) | 125.7 | 55.3 | 69.2 | 157 |
| same, under `perf record -g` | 126.2 | 56.0 | 68.9 | 155 |
| buffered IO, inputs in page cache | 56.8 | 56.6 | 153 | 154 |
| 32 batches → large (DIRECTIO) | 137.7 | 66.0 | 60.9 | 127 |

With DIRECTIO the merge thread is on CPU only 44% of the time on this gp3 volume. Posting reads and writes are
synchronous O_DIRECT I/Os of up to 512 KiB (`allocComprBuf(65536, 32768)`, 8-byte units) with no read-ahead.
Production shows 0.8 core, so its disks are faster, but async or double-buffered I/O could remove most of the
remaining 20%. CPU-bound,
one core fuses 154-157 MB/s of input on this corpus. Production does about 75 MB/s at 0.8 core, roughly 94 MB/s per
core, so the production index costs more per byte. More fields or longer documents could explain that, but I have
no evidence either way.

`perf stat` over the whole DIRECTIO run: 246.5G cycles, 702.5G instructions (IPC 2.85), 2.12G branch misses, 1.74G
L1D misses, 0.36G L2 misses. Per input posting that is 333 cycles and 950 instructions, with 3.04 positions and
82 bits per posting.

### Profile

Self time over the run (cycles, 499 Hz, frame-pointer call graphs, 48k samples):

| % | symbol |
|---|---|
| 32.9 | `Zc4PostingReader<true>::read_doc_id_and_features` |
| 9.6 | `EG2PosOccDecodeContext<true>::readFeatures` |
| 8.3 | `Zc4PostingWriterBase::calc_skip_info` |
| 7.8 | `EncodeContext64EBase<true>::writeBits` |
| 7.4 | `FeatureEncodeContext<true>::writeBits` |
| 3.4 | `Zc4PostingReaderBase::NoSkip::read` |
| 3.3 | `PostingPriorityQueue<FieldReader>::adjust` |
| 3.3 | `Zc4PostingWriter<true>::write_docid_and_features` |
| 3.3 | `FieldReader::read` |
| 2.2 | `PostingPriorityQueueMerger::mergeHeap` |
| 2.1 | `FeatureEncodeContext<true>::writeBytes` |
| 1.9 | `__memmove_avx512_unaligned_erms` |
| 1.9 | `Zc4PostingReaderBase::read_common_word_doc_id` |
| 1.4 | `NoSkip::check_not_end` |
| 1.1 | `PageDict4Reader::setupPage` |
| 1.0 | `FlushToken::stop_requested` (a virtual call per posting in the merge loops) |

Inclusive: `FieldReader::read` 56.0%, `read_doc_id_and_features` 48.4%, writer `flush_word_with_skip` 17.9%,
`write_docid_and_features` 13.1%. The production self-time profile (66.6 / 7.0 / 5.0 / 4.7 / 3.6%) has the same top
symbol and the same set of symbols, with even more of its samples in that top entry.

### The hot loop

`perf annotate` of `read_doc_id_and_features`. The same two instructions are at `0x3f0a66`/`0x3f0a6d` in the
production 8.679.50 library:

```
         :  uint32_t get_field_length() const { return _field_length; }
    0.00 :   403046: mov    0x108(%rbx),%rax      # 8-byte load of _no_skip._field_length + _num_occs
         :  void set_field_length(uint32_t val) { _field_length = val; }
   91.99 :   40304d: mov    %rax,0x4(%r12)        # 8-byte store into DocIdAndFeatures
         :  _decodeContext->readFeatures(features);
    0.00 :   403052: mov    0x2b0(%rbx),%rdi
    0.00 :   40305c: mov    (%rdi),%rax
    0.00 :   40305f: call   *0x98(%rax)           # EG2PosOccDecodeContext<true>::readFeatures
```

92% of the function's cycle samples sit on one store that runs once per posting. A second recording with
`-e cycles -e instructions` puts 94.5% of the function's retired-instruction samples on the same instruction,
while the function as a whole gets 39% of all retired-instruction samples. One instruction out of about 950 per
posting cannot retire 37% of all instructions. So this is not work done in `read_doc_id_and_features`. It is where
the core is stalled when the PMU interrupt arrives, and the EC2 guest has no IBS to attribute precisely.

What it waits on: GCC merges `features.set_field_length(_no_skip.get_field_length())` and the `num_occs` copy into
one 8-byte load and store. `NoSkip::read()` (or, for rare words, the inline decode a few instructions earlier) has
just written those two fields with separate 4-byte stores, so the 8-byte load cannot be forwarded from the store
buffer. Zen 5 `ls_bad_status2.stli_other` (0x24/0x02, "load unable to complete due to a non-forwardable conflict
with an older store") counts 3.30G events in the buffered run, 4.4 per posting.

Experiment: a compiler barrier between the two copies, giving two forwardable 4-byte copies:

| | stli_other | cycles | `read_doc_id_and_features` self | `readFeatures` self |
|---|---|---|---|---|
| as built | 3.30G | 246.3G | 32.9% | 9.6% |
| two 4-byte copies | 2.59G | 249.8G | 3.8% | 32.3% |

The conflict count went down, cycles did not, and the samples moved to the bit-decode instructions in
`readFeatures` (spread over its `or`/`shrx` ops). The forwarding failure marks where the per-posting serial chain
stalls. It is not the bottleneck itself. The experiment was reverted and is not in the commit.

### What fusion spends per posting (ablation)

The same 2.08M + 40k documents and docids were indexed three ways and fused, buffered IO with a warm cache, so the
runs are CPU-bound.

| index | posting bytes | CPU s | cycles | instructions | IPC | branch-misses | stli_other | MB/s per core |
|---|---|---|---|---|---|---|---|---|
| positions + interleaved (baseline) | 7.62 GB | 54.9 | 246.3G | 708.5G | 2.88 | 2.09G | 3.30G | 158 |
| first occurrence only (1 position per posting) | 5.83 GB | 48.2 | 217.2G | 668.0G | 3.08 | 1.09G | 2.16G | 143 |
| positions, no interleaved features | 5.43 GB | 48.3 | 217.6G | 604.7G | 2.78 | 1.99G | 0.53G | 135 |

Same 740M postings in all three.

- Each extra position (1.51G fewer) costs 19.3 cycles, 27 instructions and 0.66 branch misses. That covers the
  exp-golomb parse that raw mode still does to find each document's feature bit length
  (`UC64_SKIPEXPGOLOMB_SMALL_NS` per position in `EG2PosOccDecodeContext::readFeatures`) and three bit-level copies:
  `RawFeaturesCollector` into the blob, `writeFeatures` into the writer's feature buffer, and `writeBits` into the
  output stream at flush. With 3.04 positions per posting, positions cost about 58 cycles of the 333 (17.5%).
- Interleaved features (field length and occurrence count in the docid stream, from `enable-bm25`) cost 38.8 cycles
  and 140 instructions per posting (12%). They add 2.19 GB, 29% of the posting file, and cause 84% of the
  store-forwarding conflicts.
- The remaining ~236 cycles per posting do not depend on positions. They go to a chain of virtual calls per
  posting (`FieldReader::read` → `Zc4PostingSeqRead::readDocIdAndFeatures` → `read_doc_id_and_features` →
  `read_common_word_doc_id` → `NoSkip::read`, then virtual `readFeatures`, and the same on the writer side), with
  the bit-decoder state stored to and reloaded from memory around every call. They also cover skip-list
  consistency checks (`L1Skip::check`, `check_not_end`) on every posting, the 9-way heap merge, docid mapping, the
  writer's per-document vector push, `calc_skip_info`, and `writeBytes`, which copies each byte of the
  docid/skip buffers through `writeBits(c, 8)` (about 25 instructions per byte, 10-12% of all cycles and
  instructions) even though the stream is byte-aligned there (`e.smallAlign(8)` right before `write_zc_view`).

So "read_doc_id_and_features dominates" means the per-posting serial decode chain dominates, and the profiler parks
it on that store. A positions-heavy field makes it worse, but on this corpus positions are less than a fifth of the
cost.

### Estimates, MB/s of input per core (baseline 153-158)

| change | basis | gain | MB/s per core |
|---|---|---|---|
| cheaper raw copy: copy feature runs between docs without parsing each position | upper bound = removing 58 of 333 cycles per posting, measured by the ablation above; the copy itself still costs something | at most 1.21x | at most about 190 |
| `writeBytes` as a word copy when byte-aligned | 10-12% of cycles and instructions in `EncodeContext64EBase<true>::writeBits` + `writeBytes` | about 1.1x | about 170 |
| both, plus fewer per-posting virtual calls and checks | above, rest unmeasured | about 1.3x | about 200 |
| word-range-parallel single-field merge | measured proxy: N concurrent independent single-field fusions, 2.18 GB medium index + 8 small, buffered, outputs on tmpfs | almost linear | see below |

| concurrent merges | 1 | 2 | 4 | 8 | 16 | 24 |
|---|---|---|---|---|---|---|
| aggregate input MB/s | 173 | 343 | 683 | 1364 | 2660 | 3820 |
| per merge, MB/s per CPU-second | 174.3 | 174.1 | 173.5 | 172.8 | 169.7 | 164.3 |

Per-core efficiency drops only 6% at 24 streams, because each stream is a sequential CPU-bound decode. A body merge
split into 4 dictionary ranges would need about 0.6-0.7 GB/s of read and write bandwidth, so the disks become the
limit before the CPU does. The real implementation needs a dictionary split point per range, separate output
segments (or a second pass to place them), and a merged dictionary and counts. That overhead is not in this proxy.

## Task 3: issue draft and kernel check

The issue is in [ISSUE_DRAFT.md](ISSUE_DRAFT.md), following the feature-request template plus version and
environment.

MADV_SEQUENTIAL check:

- `search.mmap.advise` defaults to `SEQUENTIAL` (`configdefinitions/src/vespa/proton.def:185`). It becomes
  `posix_madvise(..., POSIX_MADV_SEQUENTIAL)` on every mmapped disk-index file (`fastos/unix_file.cpp:165`, through
  `TuneFileRandRead::setFromMmapConfig` and `bootstrapconfigmanager.cpp:84`). The kernel sets `VM_SEQ_READ` on the
  VMA (`mm/madvise.c:1033` in v6.1).
- Linux v6.1 `mm/rmap.c:832-843`, `folio_referenced_one()`: `ptep_clear_flush_young_notify()` clears the young bit,
  but `referenced++` runs only `if (likely(!(vma->vm_flags & VM_SEQ_READ)))`. The comment says "Don't treat a
  reference through a sequentially read mapping as such". With MGLRU, line 826-830 also skips
  `lru_gen_look_around()` for `VM_SEQ_READ | VM_RAND_READ`, and `vmscan.c:3752` `should_skip_vma()` skips those VMAs
  in the aging walk. `zap_pte_range()` (`memory.c:1442`) does not `mark_page_accessed()` young pages of such VMAs
  on unmap.
- Identical in the latest stable `linux-6.1.y` (6.1.188, `rmap.c:842`) and in Amazon Linux's
  `kernel-6.1.188-233.386.amzn2023` tag (`rmap.c:842`, `memory.c:1447`, `vmscan.c:3778`), which is the kernel the
  benchmark box ran.
- From v6.3 on (checked v6.3, v6.6, v6.12 and master 7.3-rc5), `invalid_folio_referenced_vma()` skips any VMA with
  `!vma_has_recency(vma)`, i.e. `VM_SEQ_READ` or `VM_RAND_READ` or `FMODE_NOREUSE`. On newer kernels
  `search.mmap.advise=RANDOM` loses references too. Only `NORMAL` keeps them.
- `folio_check_references()` (`vmscan.c:1446`) returns RECLAIM when no PTE references are counted. Refault
  activation (`workingset.c`) only activates when the refault distance is within the active file list, which stays
  empty if nothing is ever activated.

Reproduction on the box (kernel 6.1.188-233.386.amzn2023, `lru_gen` off). `report-artifacts/madv_hot_test.c` maps
an 8 GiB file, uses the first 256 MiB as a hot set touched 16 times per round, streams 256 MiB of cold pages per
round, and runs in a 2 GiB `MemoryMax` cgroup, 24 rounds per advice value, caches dropped in between:

| advice | `active_file` in the cgroup | hot set fully re-read | hot-set major faults per round, rounds 4-23 |
|---|---|---|---|
| NORMAL | 0 MB until round 19, 256 MB (the hot set) from round 20 on | rounds 7 and 13, then never | 205 (0 from round 14) |
| SEQUENTIAL | 0 MB in every round, `workingset_activate_file` 0 | rounds 7, 13 and 20, every ~6.5 rounds | 307 |

With SEQUENTIAL nothing in the cgroup is ever activated, so the hot set rotates out with the cold stream. With
NORMAL it is activated after two passes and stops faulting. A first version of the test touched the hot set once
per round and showed no difference between the two (`results/madvise/result3-*`). Most likely every mapped page
started out referenced once and reclaim passed the whole list twice between hot touches, so that shape cannot show
protection. I mention it so nobody re-uses it. The RANDOM run was still in progress when I stopped. On 6.1, RANDOM references count in the
classic LRU path (`rmap.c:842` only checks `VM_SEQ_READ`).

## How to reproduce

```
# benchmark (built with -DEXCLUDE_TESTS_FROM_ALL=TRUE: make searchlib_sourceblender_benchmark_app)
taskset -c 8 searchlib_sourceblender_benchmark_app --children=zc,array            # 50M docids, k=1..15, stale 0/2/10%
taskset -c 8 searchlib_sourceblender_benchmark_app --children=zc --docs=460000000 --large=5900000 \
    --k=1,5,10,15 --stale=0,0.02 --reps=5
# counters for the timed loops only (FIFOs shared with a container work too)
mkfifo ctl ack; perf stat -C 8 -D -1 --control fifo:ctl,ack -e cycles,instructions,branch-misses,\
cpu/event=0x64,umask=0xf8/,cpu/event=0x64,umask=0x08/ -- sleep 1000 &
PERF_CTL_FIFO=ctl PERF_ACK_FIFO=ack taskset -c 8 searchlib_sourceblender_benchmark_app --impl=legacy --k=10 ...
# fusion (tool added to searchlib/src/tests/diskindex/fusion/CMakeLists.txt as searchlib_fusion_profile_app)
fusion_profile build <dir> <docids.txt> CC-MAIN-…-000NN.warc.wet.gz …
FUSION_IO=normal perf stat -e cycles,instructions,cpu/event=0x24,umask=0x02/ -- fusion_profile fuse out 4 large small0 … small7
```

Scripts used on the box are in `report-artifacts/scripts/`, raw outputs in `report-artifacts/results/`.

## Checked

- Traced: old and new strict blenders produce the same hit sequence. Both find the smallest docid ≥ target whose
  selected child is positioned on it, both apply the same end-id and docid-limit rules, and the corner case of
  seeking exactly to a hit beyond the end id is handled in the `candidate >= limit` branch. Also traced: the cached
  docids stay in sync, because only the blender moves children and `initRange`/`transform_children` resync.
- Measured: every number in the tables above, and the build, tests and benchmark of both patches together on
  `agents/serpapi-vespa-perf` (`39baddc7e0a`) and `agents/serpapi-8.679.50` (`801cfcd4314`).
- Assumed: the production "apple" children behave like the benchmark's Zc4 iterators, and the production fusion
  sample sink is the same instruction (the bytes match, the sample distribution can't be checked from here).
- Needs execution: the production CPU saving per query, which needs a canary node, and a real word-range-parallel
  or raw-copy implementation.
