#!/usr/bin/env python3
"""Replay a production memory-index flush trace through disk-index merge policies.

Input: TSV lines "<unix_ts>\t<vespa.log message>" holding proton flush.complete events for
webpage.0.ready (memoryindex.flush and memoryindex.fusion), as pulled read-only from vespa.log.
The fusion events calibrate the merge throughput (bytes written per second of fusion time).

Model:
- one merge worker (Vespa runs one fusion at a time), merge time = output bytes / throughput;
- a merge's output size = sum of inputs * keep, keep = (base growth) calibrated from the trace
  (fused index grew far less than the flushed bytes: most flushed postings replace stale ones);
- sources searched = base + intermediates + flushed + 1 memory index;
- peak extra disk = output bytes of the running merge (inputs are kept until it completes).
"""
import sys, re, json, math, collections

def load(path):
    flushes, fusions = [], []
    for line in open(path):
        p = line.rstrip("\n").split("\t")
        m = re.search(r'value="(\{.*\})"', p[-1])
        if not m: continue
        try: v = json.loads(m.group(1))
        except ValueError: continue
        n = v.get("name", "")
        size = v.get("output", {}).get("size", 0)
        if n == "webpage.0.ready.memoryindex.flush": flushes.append((float(p[0]), size))
        elif n == "webpage.0.ready.memoryindex.fusion": fusions.append((float(p[0]), size, v["time.elapsed.ms"] / 1000))
    return sorted(flushes), sorted(fusions)

class Seg:
    __slots__ = ("size", "level")
    def __init__(s, size, level): s.size, s.level = size, level

def simulate(flushes, base0, thr, keep, policy, **kw):
    t_end = flushes[-1][0]; t0 = flushes[0][0]
    base = Seg(base0, 99); segs = []        # segs: flushed (level 0) and intermediates (level>=1)
    busy_until = None; merging = None       # (inputs, output Seg, include_base)
    written = 0.0; busy = 0.0
    src_area = 0.0; max_src = 0; last_t = t0; peak_extra = 0
    events = collections.deque(flushes)
    t = t0
    ids_since_full = 0                      # every flushed memory index consumes a source id
    def nsrc(): return 1 + len(segs) + 1   # base + segs + memory index
    while events or merging:
        nxt = events[0][0] if events else math.inf
        tm = busy_until if merging else math.inf
        t_new = min(nxt, tm)
        if t_new == math.inf or t_new > t_end: break
        src_area += nsrc() * (t_new - last_t); last_t = t_new; t = t_new
        if tm <= nxt:
            inputs, out, with_base = merging
            for s in inputs: segs.remove(s)
            if with_base: base = out; ids_since_full = len(segs)
            else: segs.append(out)
            merging = None
        else:
            _, size = events.popleft(); segs.append(Seg(size, 0)); ids_since_full += 1
        max_src = max(max_src, nsrc())
        if not merging:
            plan = policy(base, segs, ids_since_full=ids_since_full, **kw)
            if plan:
                inputs, with_base, level = plan
                in_bytes = sum(s.size for s in inputs) + (base.size if with_base else 0)
                new_bytes = sum(s.size for s in inputs)
                out_size = (base.size + new_bytes * keep) if with_base else new_bytes * max(keep, 0.85)
                out = Seg(out_size, 99 if with_base else level)
                dur = out_size / thr
                merging = (list(inputs), out, with_base); busy_until = t + dur
                written += out_size; busy += min(dur, t_end - t); peak_extra = max(peak_extra, out_size)
    days = (t - t0) / 86400
    flushed = sum(s for _, s in flushes if _ <= t)
    return dict(days=days, wa=(written + flushed) / flushed, gb_day=written / 1e9 / days,
                busy_h_day=busy / 3600 / days, mean_src=src_area / (t - t0), max_src=max_src,
                peak_extra_gb=peak_extra / 1e9, base_gb=base.size / 1e9)

# ---- policies: return (inputs, include_base, output_level) or None ----
def vespa_current(base, segs, trigger=1, ids_since_full=0):
    # FusionRunner: base + every flushed index. `trigger` = flushed indexes needed before a fusion
    # starts; calibrated below so the simulated WA matches the measured one for this trace.
    return (list(segs), True, 99) if len(segs) >= trigger else None

def tiered(base, segs, fanout=4, full_ratio=0.10, max_ids=200, ids_since_full=0):
    # Intermediate fusion (ISSUE_DRAFT, prototype branch serpapi-8.781.11-tiered-fusion): merge `fanout`
    # segments of the same level into level+1; full fusion into the base when intermediates exceed
    # full_ratio of the base, or when the relative source ids (limit 254) pass max_ids.
    if sum(s.size for s in segs) > full_ratio * base.size or ids_since_full > max_ids:
        return (list(segs), True, 99)
    by = collections.defaultdict(list)
    for s in segs: by[s.level].append(s)
    for lvl in sorted(by):
        if len(by[lvl]) >= fanout: return (by[lvl][:fanout], False, lvl + 1)
    return None

def size_tiered(base, segs, fanout=10, floor=2e9, ids_since_full=0):
    # Lucene TieredMergePolicy / Tantivy LogMergePolicy shape: merge `fanout` segments of similar
    # size (log buckets above `floor`); the base takes part when the top tier reaches its size.
    def bucket(sz): return 0 if sz <= floor else int(math.log(sz / floor, fanout)) + 1
    by = collections.defaultdict(list)
    for s in segs: by[bucket(s.size)].append(s)
    for b in sorted(by):
        if len(by[b]) >= fanout:
            grp = by[b][:fanout]
            if sum(s.size for s in grp) >= base.size: return (grp, True, 99)
            return (grp, False, b + 1)
    return None

if __name__ == "__main__":
    flushes, fusions = load(sys.argv[1])
    thr = sum(f[1] for f in fusions) / sum(f[2] for f in fusions)
    base0 = fusions[0][1]
    flushed_total = sum(s for _, s in flushes)
    keep = (fusions[-1][1] - fusions[0][1]) / sum(s for ts, s in flushes if fusions[0][0] < ts <= fusions[-1][0])
    print(f"trace: {len(flushes)} flushes, {flushed_total/1e9:.0f} GB over {(flushes[-1][0]-flushes[0][0])/86400:.1f} d; "
          f"{len(fusions)} fusions, base {base0/1e9:.0f} -> {fusions[-1][1]/1e9:.0f} GB; "
          f"fusion throughput {thr/1e6:.0f} MB/s written; keep ratio {keep:.3f}")
    meas = sum(f[1] for f in fusions)
    print(f"measured: fusion wrote {meas/1e9/((fusions[-1][0]-fusions[0][0])/86400):.0f} GB/day, WA {(meas+flushed_total)/flushed_total:.0f}x\n")
    measured_wa = (meas + flushed_total) / flushed_total
    trigger = min(range(1, 13), key=lambda k: abs(simulate(flushes, base0, thr, keep, vespa_current, trigger=k)["wa"] - measured_wa))
    rows = [(f"vespa current, calibrated (trigger {trigger})", vespa_current, dict(trigger=trigger)),
            ("tiered fanout 4, full at 10% of base", tiered, dict(fanout=4, full_ratio=0.10)),
            ("tiered fanout 4, full at 25% of base", tiered, dict(fanout=4, full_ratio=0.25)),
            ("tiered fanout 8, full at 10% of base", tiered, dict(fanout=8, full_ratio=0.10)),
            ("size-tiered fanout 10 (Lucene TMP-like)", size_tiered, dict(fanout=10)),
            ("size-tiered fanout 4", size_tiered, dict(fanout=4))]
    print(f"{'policy':42} {'WA':>6} {'GB/day':>7} {'busy h/d':>8} {'mean src':>8} {'max src':>7} {'peak+GB':>7}")
    for name, pol, kw in rows:
        r = simulate(flushes, base0, thr, keep, pol, **kw)
        print(f"{name:42} {r['wa']:6.1f} {r['gb_day']:7.0f} {r['busy_h_day']:8.1f} {r['mean_src']:8.1f} {r['max_src']:7d} {r['peak_extra_gb']:7.0f}")
