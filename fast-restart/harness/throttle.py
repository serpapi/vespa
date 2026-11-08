#!/usr/bin/env python3
"""Passes stdin lines (one JSON document operation each) to stdout at a fixed rate · spec: fast-restart/PREREG.md H4
usage: throttle.py LINES_PER_SECOND"""
import sys, time

rate = float(sys.argv[1])
start = time.monotonic()
out = sys.stdout
for n, line in enumerate(sys.stdin):
    ahead = n / rate - (time.monotonic() - start)
    if ahead > 0:
        out.flush()
        time.sleep(ahead)
    out.write(line)
