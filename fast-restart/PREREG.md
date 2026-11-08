# PREREG: a content node that restarts fast by design, without fighting fusion

Written 2026-11-08 before any local measurement. Results go in `REPORT.md`; this file is not edited after the
first local run, except to append dated amendments at the end.

## Baseline already measured (production, read-only, before this file)

Source: `vespa.log` event lines of all 12 production content nodes (r9g.16xlarge, Vespa 8.679.50, about 465M
documents per node), pulled read-only with ansible on 2026-11-08 and covering 2026-10-08..11-07.

- Restart of content-1 on 2026-10-18 (the only proton start inside log retention): components initialised at
  320 s (document meta store 97 s, attributes up to 216 s each, in parallel; document store 113 s; disk index
  open 11 ms on a 932 GB fused index). Then transaction log (TLS) replay of 14.57M operations took 1,262 s
  (11.5k op/s). Proton reported `started` at 1,585 s. Replay is 80 % of the restart.
- The replay low-water mark was set by the two largest string attributes. Their flushes rewrite the whole
  attribute: `content_hash` 12 GB in 864 s, `path` 29 GB in 474 s, 1-4 times a day.
- Disk index fusion over 30 days: 53 TB/day written by fusion across the fleet against 391 GB/day of flushed
  memory index, a write amplification (WA) of 136x (content-1: 145x). Fusion is busy 13-16 h/day per node.
- `flush.memory.maxtlssize` is the default 20 GiB, so the replay tail is bounded only by 20 GiB of TLS.

## Questions

Q1. Where does restart time go on a blueprint content node (i8g.4xlarge) with a production-shaped index, and
    where does replay CPU go?
Q2. Is replay time set by how far behind the slowest flush target is (a flush-cost problem) or by replay
    throughput per operation (a replay-engine problem)?
Q3. How much do merge policies used elsewhere (Lucene TieredMergePolicy, RocksDB/Tantivy size-tiered,
    the tiered fusion proposed in `ISSUE_DRAFT.md`) cut fusion WA on the production flush trace, and at what
    cost in searched sources?
Q4. Can a prototype on Vespa 8.781.11 show a restart bounded by component load time plus a small constant,
    while flush and fusion bytes written per day stay at or below today's?

## Setup

- Node: one i8g.4xlarge spot instance (16 vCPU Graviton4, 128 GiB, 3.75 TB NVMe) in the greenfield-bench VPC,
  Amazon Linux 2023, stock `vespaengine/vespa:8.781.11`, and the fork builds of `serpapi-8.781.11` and its
  prototype branches, with only the changed libraries swapped in (as the production fork image does).
- Schema: the deployed `webpage` schema (si-rollback-vespa-4a34), via R6's `make_schema.sh`.
- Corpus: 50M synthetic documents from R6's generator, body length raised so the mean document is about
  1.4 KB (production TLS: about 1.4 KB per operation).
- Restart measurements: `vespa-stop-services` is not used for the unclean case; proton is killed with
  SIGKILL after a controlled number of operations since the last flush of each target. Times come from
  proton's own event log (`Start initializing`, `load.*.complete`, `transactionlog.replay.*`, `started`).
- Each restart point is repeated 3 times; medians are reported with the range.

## Hypotheses and pass rules

H1 (Q1, measurement). With a TLS tail of at least 5M operations, replay is more than 50 % of restart time on
the i8g node, as in production. Report the breakdown and a `perf` profile of replay.

H2 (Q2). Replay throughput is limited by per-operation work on the replay path, not by I/O: replay reads the
TLS at under 25 % of the NVMe's sequential read bandwidth, and the top of the replay `perf` profile is in
document deserialisation and the feed pipeline. Falsified if replay is I/O bound (more than 50 % of the time
in I/O wait or the read rate is near device bandwidth).

H3 (Q3, simulation on the production trace). A tiered fusion policy with fanout 4 and a full fusion when
intermediates reach 25 % of the base index has a WA of at most 15x on every simulated node, with a mean number
of searched sources no higher than today's calibrated policy. Size-tiered fanout 10 (Lucene-like) is reported
for comparison but is not expected to pass the sources condition.

H4 (Q4, prototype). For any prototype claimed as a restart fix: restart time after SIGKILL at the worst point
of a steady feed is at most component load time + 60 s on the i8g node, and flush plus fusion bytes written
per hour in the same steady feed are at most 1.1x the stock build's. A prototype that shortens restart by
writing more (the "lower maxtlssize" knob alone) fails H4 by construction; it is measured as the control
that shows the trade-off.

H5 (Q4, tiered fusion prototype, if built). On a steady feed long enough for at least 3 stock fusions, the
prototype writes at least 5x fewer fusion bytes than stock, with no lost documents (`hitcount` of a match-all
query and of 20 fixed term queries identical to stock at the same serial number).

## Not in scope

No change to production. No upstream issue or PR. Query latency is measured only as a guard (p50/p99 of a
fixed query set before and after, reported, not a pass rule).
