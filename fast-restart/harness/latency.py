#!/usr/bin/env python3
"""Query latency during the fusion A/B feed · spec: fast-restart/PREREG.md (latency guard)

usage: latency.py ARM_A/queries.csv ARM_B/queries.csv
Each CSV comes from `r6load query` (open loop; latency from the scheduled send time). Only queries scheduled while
the feed ran are compared ([feed.start, feed.end] of the arm directory next to the CSV). Reports p50/p95/p99 per arm
and B/A ratios with 95 % CIs from a block bootstrap over 60-second blocks (latency is autocorrelated in time).
"""
import csv, os, random, sys

def load(path):
    d = os.path.dirname(path)
    t0 = float(open(os.path.join(d, "feed.start")).read()); t1 = float(open(os.path.join(d, "feed.end")).read())
    rows = []
    for r in csv.DictReader(open(path)):
        if None in (r.get("start_us"), r.get("latency_us"), r.get("status")):
            continue  # the client is killed at the end of the feed; its last line can be cut
        rows.append((int(r["start_us"]) / 1e6, int(r["latency_us"]) / 1000, int(r["status"])))
    # start_us is relative to the query client's start, which precedes feed.start by a few seconds at most;
    # keep the first (t1 - t0) seconds.
    dur = t1 - t0
    return [(t, lat, st) for t, lat, st in rows if t <= dur]

def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))]

def blocks(rows, size=60):
    b = {}
    for t, lat, st in rows:
        b.setdefault(int(t // size), []).append(lat)
    return list(b.values())

def boot(ba, bb, p, n=2000):
    rnd = random.Random(1)
    out = []
    for _ in range(n):
        xa = [x for blk in (rnd.choice(ba) for _ in ba) for x in blk]
        xb = [x for blk in (rnd.choice(bb) for _ in bb) for x in blk]
        out.append(pct(xb, p) / pct(xa, p))
    out.sort()
    return out[int(0.025 * n)], out[int(0.975 * n)]

a, b = load(sys.argv[1]), load(sys.argv[2])
na, nb = os.path.basename(os.path.dirname(sys.argv[1])), os.path.basename(os.path.dirname(sys.argv[2]))
ba, bb = blocks(a), blocks(b)
print(f"| percentile | {na} ms | {nb} ms | {nb}/{na} | 95 % CI |")
print("|---|---|---|---|---|")
for p in (50, 95, 99):
    la, lb = pct([x for _, x, _ in a], p), pct([x for _, x, _ in b], p)
    lo, hi = boot(ba, bb, p)
    print(f"| p{p} | {la:.1f} | {lb:.1f} | {lb/la:.3f} | {lo:.3f}-{hi:.3f} |")
for name, rows in ((na, a), (nb, b)):
    err = sum(1 for _, _, st in rows if st != 200)
    print(f"{name}: {len(rows)} queries during feed, {err} non-200")
