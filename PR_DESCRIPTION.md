make strict source blending cost independent of the number of index sources

`SourceBlenderSearchStrict` walked all children for every candidate docid. For each one it chased
`_sources[_children[i]]`, pushed the children at the minimum into a vector, and looked the minimum up in the source
selector. `doSeek()` also probed the selector and called `seek()` on every child before getting there. A node with
one fused index and a handful of flushed ones (11 on average for us, because `index.maxflushed=8` and fusion takes
about 2 h) paid that for every hit. For a one-word query matching 5.9M docs per node, the strict blender was 18% of
query CPU.

Now the blender keeps the children's current docids in a small array, updated only for the children it moves. It
also tracks the child at the smallest docid and the smallest docid of all the other children. While that child
stays below the bound, which is the usual shape of one big fused index plus small flushed and memory indexes, a hit
costs one `doSeek()` on that child, one selector lookup and one pointer compare. That cost does not depend on the
number of sources. Ties, and the leader passing the bound, fall back to a branch-free rescan of the array. Results
are unchanged, including the old behaviour of reporting a hit when seeking exactly to it beyond the end id.

ns per returned hit on AMD EPYC 9R45 (r8a/c8a), one pinned core. The big child has 5.9M postings over 460M docids,
each small child 5k postings, 2% of the big child's docs belong to newer sources. Real Zc4 disk-index iterators with
positions, min of 5 runs:

| sources | no unpack | with unpack |
|---|---|---|
| 2 | 11.31 → 8.81 (-22%) | 37.07 → 31.22 (-16%) |
| 6 | 13.43 → 8.65 (-36%) | 39.03 → 31.39 (-20%) |
| 11 | 16.43 → 8.84 (-46%) | 42.19 → 31.54 (-25%) |
| 16 | 19.62 → 9.13 (-53%) | 46.15 → 31.90 (-31%) |

- An extra source used to cost 0.59 ns per hit (0.65 with unpack). Now it costs 0.02 (0.05).
- perf stat at 11 sources: 332 → 150 instructions per hit, 74 → 40 cycles.
- With 500k postings in every child (no dominant source) it is still 9-32% faster.

Tests:

- 5 new cases in `sourceblender_test`: exhausted children; equal docids in several children; the selector picking a
  source that lacks the doc or has no child; docid limit and end id; and a randomized differential test.
- The randomized test covers 1000 random setups with mixed strict child types, random seeks, re-initialized ranges
  and unpack. It compares every step against the previous implementation, kept in
  `legacy_strict_source_blender.h`, and against a brute-force model of the selector and postings.
- The new edge-case tests also pass against the old code.
- searchlib queryeval tests and the searchcore matching and index tests pass.

`searchlib_sourceblender_benchmark_app` is a standalone benchmark, not run by ctest:

```
searchlib_sourceblender_benchmark_app --children=zc,array --k=1,3,5,10,15 --stale=0,0.02,0.1
searchlib_sourceblender_benchmark_app --docs=460000000 --large=5900000 --k=1,5,10,15 --stale=0,0.02 --reps=5
```

It runs the old and new blender on the same data in the same process, and fails on any difference in hits or in
unpacked match data.

I confirm that this contribution is made under the terms of the license found in the root directory of this
repository's source tree and that I have the authority necessary to make this contribution on behalf of its
copyright owner.
