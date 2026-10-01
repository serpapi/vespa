#!/bin/bash
# Same benchmark configurations as REPORT.md, on both bases, pinned to core 8, one at a time.
set -uo pipefail
for T in integ prod; do
  B=/work/$T/build/searchlib/src/tests/queryeval/sourceblender/searchlib_sourceblender_benchmark_app
  docker exec vbuild bash -lc "mkdir -p /work/bench/$T && cd /work/bench/$T && taskset -c 8 $B --dir=/work/bench/$T/idx --children=zc --docs=460000000 --large=5900000 --small=5000 --k=1,5,10,15 --stale=0,0.02 --unpack=0,1 --reps=5 > /work/results/bench-$T-proddensity.txt 2>&1"
  echo "[$(date -u +%T)] $T proddensity exit=$?"
  docker exec vbuild bash -lc "cd /work/bench/$T && taskset -c 8 $B --dir=/work/bench/$T/idx --children=zc,array > /work/results/bench-$T-50m.txt 2>&1"
  echo "[$(date -u +%T)] $T 50m exit=$?"
done
grep -l "MISMATCH\|FAILED" /work/results/bench-*-proddensity.txt /work/results/bench-*-50m.txt || echo "no mismatches"
echo bench-done
