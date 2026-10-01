#!/bin/bash
# usage: build.sh <integ|prod> <jobs>
set -uo pipefail
T=$1; J=$2
log() { echo "[$(date -u +%T)] $T $*"; }
python3 /work/targets.py /work/$T > /work/results/targets-$T.txt
TARGETS=$(sed -n 1p /work/results/targets-$T.txt)
log "targets: $(echo $TARGETS | wc -w) executables, $(sed -n 2p /work/results/targets-$T.txt | wc -w) tests"
docker exec vbuild bash -lc "cd /work/$T && cmake -S . -B build -DEXCLUDE_TESTS_FROM_ALL=TRUE > /work/logs/cmake-$T.log 2>&1" || { log cmake-failed; tail -30 /work/logs/cmake-$T.log; exit 1; }
log configured
docker exec vbuild bash -lc "cd /work/$T/build && make -k -j$J $TARGETS > /work/logs/make-$T.log 2>&1"; rc=$?
log "make exit=$rc"
grep -E "error:|Error [0-9]" /work/logs/make-$T.log | sort | uniq | head -30
grep -E "warning:" /work/logs/make-$T.log | sort | uniq -c | sort -rn | head -10
exit $rc
