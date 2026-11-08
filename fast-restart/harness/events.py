#!/usr/bin/env python3
"""Summarise one proton restart from vespa.log events.
usage: events.py vespa.log T_STOP T_EXIT TAG MODE OPS
T_STOP: when the stop was issued; T_EXIT: when the old proton exited (unix seconds)."""
import sys, json, re
log, t_stop, t_exit, tag, mode, ops = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), *sys.argv[4:7]
ev = []
for line in open(log, errors="replace"):
    p = line.rstrip("\n").split("\t")
    if len(p) < 7 or p[3] != "searchnode": continue
    t = float(p[0])
    if t < t_exit - 1: continue
    ev.append((t, p[6]))
def first(pat):
    return next((t for t, m in ev if re.search(pat, m)), None)
def val(m):
    mm = re.search(r'value="(\{.*\})"', m)
    return json.loads(mm.group(1)) if mm else {}
t_init = first(r"Start initializing components")
t_done = first(r"Done initializing components")
t_started = first(r'started/1 name="proton"')
loads = {}
for t, m in ev:
    if "load." in m and ".complete" in m and "webpage.0.ready" in m:
        v = val(m); kind = re.search(r'name="load\.(\w+)\.complete"', m).group(1)
        key = kind if kind != "attribute" else "attribute:" + v.get("name", "?")
        loads[key] = v.get("time.elapsed.ms", 0) / 1000
replay = {}
for t, m in ev:
    if "transactionlog.replay.start" in m and '"webpage"' in m:
        v = val(m); replay["first"], replay["last"] = v["serialnum"]["first"], v["serialnum"]["last"]; replay["t_start"] = t
    if "transactionlog.replay.complete" in m and '"webpage"' in m:
        replay["seconds"] = val(m)["time.elapsed.ms"] / 1000; replay["t_end"] = t
ops_replayed = replay.get("last", 0) - replay.get("first", 0)
attr = {k: v for k, v in loads.items() if k.startswith("attribute:")}
out = dict(tag=tag, mode=mode, ops_fed=int(ops),
           stop_s=round(t_exit - t_stop, 1),
           init_s=round(t_done - t_init, 1) if t_init and t_done else None,
           exit_to_init_s=round(t_init - t_exit, 1) if t_init else None,
           replay_ops=ops_replayed, replay_s=replay.get("seconds"),
           replay_ops_per_s=round(ops_replayed / replay["seconds"]) if replay.get("seconds") else None,
           exit_to_started_s=round(t_started - t_exit, 1) if t_started else None,
           documentmetastore_s=loads.get("documentmetastore"), documentstore_s=loads.get("documentstore"),
           max_attribute_s=max(attr.values()) if attr else None,
           slowest_attribute=max(attr, key=attr.get) if attr else None)
print(json.dumps(out))
