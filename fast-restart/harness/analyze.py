#!/usr/bin/env python3
"""Summarise fusion A/B arms and restarts · spec: fast-restart/PREREG.md

usage: analyze.py OUT_DIR VESPA_LOG
Reads OUT_DIR/<arm>/{feed.start,feed.end,settle.end,io.before,io.after,sources.txt,hits.txt} for every arm
directory starting with "ab-", the proton events in VESPA_LOG inside each arm's window, and
OUT_DIR/restarts.jsonl. Prints Markdown tables.
"""
import glob, json, os, re, sys, statistics as st

out_dir, log_path = sys.argv[1], sys.argv[2]

def events(t0, t1):
    for line in open(log_path, errors="replace"):
        p = line.rstrip("\n").split("\t")
        if len(p) < 7 or p[3] != "searchnode":
            continue
        t = float(p[0])
        if t0 <= t <= t1:
            m = re.search(r'name="([a-z._]+)" value="(\{.*\})"', p[6])
            if m:
                try:
                    yield t, m.group(1), json.loads(m.group(2))
                except ValueError:
                    pass

def io_bytes(path):
    d = dict(l.split(": ") for l in open(path).read().strip().splitlines())
    return int(d["write_bytes"]), int(d["read_bytes"])

def time_weighted_sources(path, t0, t1):
    pts = []
    for line in open(path):
        f = line.split()
        if len(f) >= 2:
            pts.append((int(f[0]), int(f[1])))
    pts = [p for p in pts if t0 <= p[0] <= t1]
    if len(pts) < 2:
        return None, None
    area = sum((b[0] - a[0]) * a[1] for a, b in zip(pts, pts[1:]))
    return area / (pts[-1][0] - pts[0][0]), max(p[1] for p in pts)

rows = []
for arm_dir in sorted(glob.glob(os.path.join(out_dir, "ab-*"))):
    arm = os.path.basename(arm_dir)
    try:
        t0 = float(open(f"{arm_dir}/feed.start").read()); t1 = float(open(f"{arm_dir}/settle.end").read())
        tf = float(open(f"{arm_dir}/feed.end").read())
    except FileNotFoundError:
        continue
    by = {}
    n_fusion = n_inter = 0
    for t, name, v in events(t0, t1):
        if name == "flush.complete" and v.get("name", "").startswith("webpage.0.ready."):
            target = v["name"][len("webpage.0.ready."):]
            key = target.split(".")[0] + "." + target.split(".")[1] if target.startswith("attribute.flush") else target
            if target.startswith("attribute.flush"):
                key = "attribute.flush.*"
            size = v.get("output", {}).get("size", 0)
            by.setdefault(key, [0, 0, 0.0])
            by[key][0] += 1; by[key][1] += size; by[key][2] += v.get("time.elapsed.ms", 0) / 1000
        if name == "fusion.complete" and "webpage" in json.dumps(v):
            out = v.get("output", {}).get("dir", "")
            if "index.inter." in out:
                n_inter += 1
            elif "index.fusion." in out:
                n_fusion += 1
    w0, r0 = io_bytes(f"{arm_dir}/io.before"); w1, r1 = io_bytes(f"{arm_dir}/io.after")
    mean_src, max_src = time_weighted_sources(f"{arm_dir}/sources.txt", t0, tf)
    hits = open(f"{arm_dir}/hits.txt").read() if os.path.exists(f"{arm_dir}/hits.txt") else ""
    rows.append(dict(arm=arm, feed_s=tf - t0, settle_s=t1 - tf, by=by, n_fusion=n_fusion, n_inter=n_inter,
                     proton_write_gb=(w1 - w0) / 1e9, proton_read_gb=(r1 - r0) / 1e9,
                     mean_src=mean_src, max_src=max_src, hits=hits))

if rows:
    print("| arm | feed s | settle s | memory index flushed GB | fusion+intermediate written GB (count) | index WA | attribute flush GB (n) | doc meta store GB (n) | proton write_bytes GB | mean / max disk indexes |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        b = r["by"]
        fl = b.get("memoryindex.flush", [0, 0, 0]); fu = b.get("memoryindex.fusion", [0, 0, 0])
        at = b.get("attribute.flush.*", [0, 0, 0]); dm = b.get("documentmetastore.flush", [0, 0, 0])
        wa = (fl[1] + fu[1]) / fl[1] if fl[1] else float("nan")
        print(f"| {r['arm']} | {r['feed_s']:.0f} | {r['settle_s']:.0f} | {fl[1]/1e9:.1f} ({fl[0]}) | {fu[1]/1e9:.1f} ({fu[0]}: {r['n_fusion']} full, {r['n_inter']} intermediate) | {wa:.1f} | {at[1]/1e9:.1f} ({at[0]}) | {dm[1]/1e9:.1f} ({dm[0]}) | {r['proton_write_gb']:.1f} | {r['mean_src'] or 0:.1f} / {r['max_src']} |")
    hit_sets = {r["arm"]: r["hits"] for r in rows}
    print("\nhit counts identical across arms:", len(set(hit_sets.values())) == 1)
    for arm, h in hit_sets.items():
        print(f"\n{arm}:\n{h}")

rp = os.path.join(out_dir, "restarts.jsonl")
if os.path.exists(rp):
    rs = [json.loads(l) for l in open(rp) if l.strip()]
    print("\n| tag | mode | ops fed | stop s | init s | replay ops | replay s | op/s | exit->started s | docmeta s | docstore s | slowest attribute |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rs:
        print(f"| {r['tag']} | {r['mode']} | {r['ops_fed']} | {r['stop_s']} | {r['init_s']} | {r['replay_ops']} | {r['replay_s']} | {r['replay_ops_per_s']} | {r['exit_to_started_s']} | {r['documentmetastore_s']} | {r['documentstore_s']} | {r['slowest_attribute']} {r['max_attribute_s']} |")
