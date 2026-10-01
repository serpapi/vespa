Title: Disk index fusion rewrites the whole fused index for every merge: write amplification grows with index size

---

**Is your feature request related to a problem? Please describe.**

Every disk index fusion merges the previous fused index and all flushed indexes into a new fused index
(`FusionRunner::fuse`: sources are `index.fusion.<last>` plus every `index.flush.<id>`, output is a new
`index.fusion.<id>`). A fusion therefore costs the size of the fused index, not the size of the new data. As the
fused index grows, the same few GB of new postings cost more and more I/O and CPU, and because fusion gets slower,
more flushed indexes pile up and get searched.

Numbers from our cluster (12 content nodes, about 460M documents per node, AWS r8a.8xlarge, one string field with
`index: enable-bm25` holding 85% of the index bytes, `index.maxflushed: 8`):

- fused index: 640-700 GB per node; flushed indexes about 1 GB each; about 23 GB/day of flushed index data per node
- one fusion takes 2.0-2.2 h at about 0.8 core and about 75 MB/s read plus 75 MB/s write
- a 700 GB fused index is rewritten about 7.85 times per day per node. That is about 5.5 TB written and 5.5 TB read
  per node per day to absorb 23 GB/day of new index data, a write amplification of about 240x. It also costs about
  13 core-hours per node per day, with a fusion running about 16.5 h of every 24
- because a fusion is running most of the time, flushed indexes accumulate behind it. The time-weighted mean number
  of searched sources (fused + flushed + memory) is 11.07, ranging from 6 to 16
- each extra source costs query time. In a CPU profile of a one-word query with 5.9M matches per node,
  `SourceBlenderSearchStrict` was 18% of query CPU. A separate PR makes the strict blender's per-source cost almost
  flat (0.59 → 0.02 ns per hit per extra source), but every source still adds a dictionary lookup, a posting
  iterator, and stale postings to skip
- each new fused index is written with `indexing.write.io: DIRECTIO` (the default), so none of it is in the page
  cache when it replaces the old one, and `index.warmup.time` defaults to 0. All hot posting pages of the biggest
  index are faulted in again after every fusion, about 8 times a day. See also the `search.mmap.advise` note under
  additional context.

We also profiled fusion of a 2.1M-document index built from Common Crawl text (one positions field with
interleaved features, Zc4) on the same CPU as r8a (AMD EPYC 9R45):

- Fusion is CPU-bound per posting: about 333 cycles per posting, 154-158 MB/s of input per core.
- The `body` field is merged by a single `FieldMerger` task. Independent merge streams scale almost linearly
  (24 concurrent merges: 3.8 GB/s, 164 MB/s per core). So a word-range-parallel merge of one field would shorten
  fusion roughly in proportion to the threads used, but it would not reduce the bytes rewritten.
- Reads and writes are synchronous `O_DIRECT` I/Os (512 KiB buffers for postings) with no read-ahead or
  write-behind. On a gp3 volume the merge thread was on CPU only 44% of the time.

**Describe the solution you'd like**

Tiered (LSM-style) fusion, so the large fused index is rewritten rarely and new data is merged in proportion to its
size.

1. Intermediate fusion. When enough flushed indexes exist (count or bytes), fuse only the flushed indexes, without
   the base fused index, into one intermediate disk index. The existing `Fusion` class takes any set of sources plus a
   `SelectorArray`, and its docid mappers already drop docids owned by sources outside the merge, so the merge
   itself should need little new code. Its cost is proportional to the new data
   (minutes, not hours).
2. Full fusion of base + intermediate + remaining flushes only when the intermediate passes a fraction of the base
   (for example 5-10%), when stale postings in the base pass a limit, or on a schedule.
3. Selector remap. Source ids are relative to the last fusion id (`FixedSourceSelector` base id,
   `cloneAndSubtract`). For an intermediate built from flushes a..b, give it id b (as fusion uses the last flush id
   today), map selector values a..b to b, and leave the base (0) and anything newer than b unchanged. Documents
   whose newest version is in a..b then resolve to the intermediate, and the base keeps its own id.
4. Disk layout and recovery. Write the intermediate to its own directory with its selector file and the serial
   number of its newest input (like a flush covering the serial range of its inputs). `IndexMaintainer` loads
   base + intermediate(s) + newer flushes. Transaction log replay starts from the newest serial number as it does
   today. `DiskIndexCleaner` removes the inputs after the switch, and a crash mid-merge leaves a partial directory
   that is removed on startup, exactly like an interrupted fusion today.
5. Stale postings. Postings of re-fed or removed documents stay in the base until the next full fusion, and the
   selector already makes queries skip them. In our microbenchmark, 10% stale postings in the big source cost
   0.5-2.7 ns per returned hit without unpack (2-5 ns with unpack). `compactLidSpace` must keep working on the remapped selector.
6. Configuration. Something like `index.fusion.intermediate.max-sources` / `max-bytes` and
   `index.fusion.full-ratio`, with defaults that keep today's behaviour.

With our numbers and one full fusion per day plus intermediate merges (each rewrites at most about 23 GB):
roughly 0.8 TB/day written instead of 5.5 TB, about 2 instead of 13 fusion core-hours per node per day, and a mean
of about 4-6 searched sources instead of 11. The page cache of the big index would survive most of the day instead
of being rebuilt 8 times.

**Describe alternatives you've considered**

- Lower `index.maxflushed`: fewer sources, but more frequent full rewrites, so even more write amplification.
- Raise `index.maxflushed`: fewer rewrites, but more sources and more query cost per term.
- Make fusion itself faster: a word-range-parallel merge of one field (almost linear in cores in our measurement),
  a raw feature copy that does not parse every position (at most 1.2x per core in our ablation), and writing
  byte-aligned docid/skip buffers with a word copy instead of `writeBits(c, 8)` per byte (about 1.1x). Worth doing,
  but none of them changes how many bytes are rewritten per day.
- More, smaller content nodes: reduces the size per node, at a hardware cost.

**Additional context**

Environment: Amazon Linux 2023, kernel 6.1 (we reproduced on `6.1.188-233.386.amzn2023`, MGLRU disabled, which is
the AL2023 default), AWS r8a.8xlarge (AMD EPYC 9R45), self-hosted.

Vespa version: 8.679.50. The relevant code is unchanged on master (`b135ebe18c7`).

`search.mmap.advise` defaults to `SEQUENTIAL` (`proton.def`), which becomes `posix_madvise(POSIX_MADV_SEQUENTIAL)`
on every mmapped disk index file (`fastos/unix_file.cpp`), i.e. `VM_SEQ_READ` on the VMA. On Linux 6.1,
`folio_referenced_one()` (`mm/rmap.c:832-843` in v6.1, also in 6.1.188 stable and in the AL2023 kernel source) clears
the young bit of such PTEs but does not count the reference ("Don't treat a reference through a sequentially read
mapping as such"). With MGLRU, `lru_gen_look_around()` and the aging walk (`should_skip_vma()`) skip these VMAs too.
`folio_check_references()` then reclaims the page as if it had never been used. Refault activation only helps
while other pages keep the active file list large; in our test nothing was ever activated.

So posting-list pages that are only read through the mmap get no LRU protection however hot they are. From 6.3 on,
`invalid_folio_referenced_vma()` skips every VMA without `vma_has_recency()` (`VM_SEQ_READ`, `VM_RAND_READ` or
`FMODE_NOREUSE`), so on newer kernels `RANDOM` loses references as well and only `NORMAL` keeps them.

Reproduction on 6.1.188 (8 GiB mmapped file, a 256 MiB hot set touched 16 times per round, 256 MiB of cold
streaming per round, 2 GiB memory cgroup):

- with `MADV_SEQUENTIAL`, `active_file` in the cgroup stays at 0 and the whole hot set is re-read from disk every
  ~6.5 rounds
- with `MADV_NORMAL`, the hot set is activated (256 MiB `active_file`) and stops faulting after the second pass

Combined with a cold 700 GB index after each fusion, this makes page cache behaviour for the largest index worse
than it needs to be. It may be worth reconsidering the default, or documenting the trade-off.
