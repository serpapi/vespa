#!/bin/bash
# usage: bench-run.sh <tag> [extra benchmark args]
set -uo pipefail
TAG=$1; shift
B=/work/vespa/build/searchlib/src/tests/queryeval/sourceblender/searchlib_sourceblender_benchmark_app
docker exec vbuild bash -lc "mkdir -p /work/bench/$TAG && cp $B /work/bench/$TAG/ && cd /work/bench/$TAG && taskset -c 8 ./searchlib_sourceblender_benchmark_app --dir=/work/bench/$TAG/idx $* > /work/results/bench-$TAG.txt 2> /work/results/bench-$TAG.err"; rc=$?
echo "[$(date -u +%T)] bench $TAG exit=$rc"; grep -c "^CSV" /work/results/bench-$TAG.txt; grep -E "MISMATCH|FAILED" /work/results/bench-$TAG.txt | head
exit $rc
