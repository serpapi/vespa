#!/usr/bin/env python3
"""H4 summary: restart time vs load time, and bytes written by group per hour of feed · spec: fast-restart/PREREG.md
(H4 amendment of 2026-11-08)
usage: h4.py OUT_DIR BASELINE_ARM ARM...   (each ARM a directory written by h4.sh)"""
import json, os, re, sys

ATTRIBUTE_TYPES = {}  # attribute name -> "numeric" | "string/multi", from the schema next to this script


def load_attribute_types():
    sd = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "app", "schemas", "webpage.sd")).read()
    for m in re.finditer(r"field (\w+) type ([\w<>]+) \{(.*?)\n        \}", sd, re.S):
        name, typ, body = m.groups()
        if "attribute" not in body:
            continue
        plain_numeric = typ in ("int", "long", "byte", "float", "double") and "fast-search" not in body
        ATTRIBUTE_TYPES[name] = "attribute: plain numeric" if plain_numeric else "attribute: other"


def group(parent, grand, great):
    names = (parent, grand, great)
    for prefix, label in (("index.inter.", "index: intermediate"), ("index.fusion.", "index: fusion"),
                          ("index.flush.", "index: flush")):
        if any(n.startswith(prefix) for n in names):
            return label
    if parent.startswith("snapshot-"):
        if grand == "documentmetastore":
            return "document meta store"
        return ATTRIBUTE_TYPES.get(grand, "attribute: other")
    if parent == "summary":
        return "document store"
    if "tls" in (grand, great):
        return "transaction log"
    return "other"


def writes(arm_dir):
    out = {}
    for line in open(os.path.join(arm_dir, "writes.txt")):
        m = re.match(r"@bytes\[(.*), (.*), (.*)\]: (\d+)", line.strip())
        if m:
            g = group(*m.groups()[:3])
            out[g] = out.get(g, 0) + int(m.group(4))
    return out


def io_write_bytes(path):
    return int(re.search(r"write_bytes: (\d+)", open(path).read()).group(1))


def summary(arm_dir):
    start = float(open(os.path.join(arm_dir, "feed.start")).read())
    t_stop = float(open(os.path.join(arm_dir, "kill.txt")).read().split()[0])
    hours = (t_stop - start) / 3600
    w = writes(arm_dir)
    restart = json.loads(open(os.path.join(arm_dir, "restart.json")).read().strip().splitlines()[-1])
    flush_fusion = sum(v for k, v in w.items() if k not in ("transaction log", "other"))
    proc = io_write_bytes(os.path.join(arm_dir, "io.kill")) - io_write_bytes(os.path.join(arm_dir, "io.before"))
    return dict(hours=hours, groups={k: v / hours for k, v in w.items()}, flush_fusion=flush_fusion / hours,
                proc=proc / hours, restart=restart)


def main():
    load_attribute_types()
    out, base, arms = sys.argv[1], sys.argv[2], sys.argv[3:]
    s = {a: summary(os.path.join(out, a)) for a in [base] + [a for a in arms if a != base]}
    names = list(s)
    gb = lambda x: f"{x / 1e9:.1f}"
    print("| | " + " | ".join(names) + " |")
    print("|---|" + "---|" * len(names))
    print("| feed minutes | " + " | ".join(f"{s[a]['hours'] * 60:.0f}" for a in names) + " |")
    for key, label in (("exit_to_started_s", "SIGKILL -> started, s"), ("init_s", "component load (init), s"),
                       ("replay_ops", "ops replayed"), ("replay_s", "replay, s")):
        print(f"| {label} | " + " | ".join(str(s[a]["restart"].get(key)) for a in names) + " |")
    print("| **H4 restart <= load + 60 s** | " + " | ".join(
        "pass" if s[a]["restart"]["exit_to_started_s"] <= s[a]["restart"]["init_s"] + 60 else "fail" for a in names) + " |")
    groups = sorted({g for a in names for g in s[a]["groups"]})
    for g in groups:
        print(f"| {g}, GB/h | " + " | ".join(gb(s[a]["groups"].get(g, 0)) for a in names) + " |")
    print("| **flush + fusion, GB/h** | " + " | ".join(f"**{gb(s[a]['flush_fusion'])}**" for a in names) + " |")
    print("| vs baseline | " + " | ".join(f"{s[a]['flush_fusion'] / s[base]['flush_fusion']:.2f}x" for a in names) + " |")
    print("| **H4 bytes <= 1.1x baseline** | " + " | ".join(
        "pass" if s[a]["flush_fusion"] <= 1.1 * s[base]["flush_fusion"] else "fail" for a in names) + " |")
    print("| proton write_bytes (/proc), GB/h | " + " | ".join(gb(s[a]["proc"]) for a in names) + " |")


if __name__ == "__main__":
    main()
