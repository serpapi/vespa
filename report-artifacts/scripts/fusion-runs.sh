#!/bin/bash
set -uo pipefail
APP=/work/vespa/build/searchlib/src/tests/diskindex/fusion/searchlib_fusion_profile_app
W=/work/fusion
SRC="$W/large $(for j in 0 1 2 3 4 5 6 7; do echo -n "$W/small$j "; done)"
EV='cycles,instructions,branch-misses,cpu/event=0x64,umask=0xf8,name=l1d_miss/,cpu/event=0x64,umask=0x08,name=l2_miss/'
run() { docker exec $1 vperf bash -lc "$2"; }
# A: clean run, DIRECTIO as proton
run "" "$APP fuse $W/out-a 4 $SRC" > $W/run-a.log 2>&1; cat $W/run-a.log
# B: perf record, DIRECTIO
run "" "cd $W && perf record -F 499 -g -o $W/perf-direct.data -- $APP fuse $W/out-b 4 $SRC" > $W/run-b.log 2>&1; grep "^fuse" $W/run-b.log
# C: perf stat, DIRECTIO
run "" "perf stat -e '$EV' -o $W/perfstat-direct.txt -- $APP fuse $W/out-c 4 $SRC" > $W/run-c.log 2>&1; grep "^fuse" $W/run-c.log; cat $W/perfstat-direct.txt
rm -rf $W/out-b $W/out-c
# D: buffered IO (inputs in page cache after first pass)
run "-e FUSION_IO=normal" "$APP fuse $W/out-d 4 $SRC" > $W/run-d1.log 2>&1; grep "^fuse" $W/run-d1.log
run "-e FUSION_IO=normal" "$APP fuse $W/out-d 4 $SRC" > $W/run-d2.log 2>&1; grep "^fuse" $W/run-d2.log
run "-e FUSION_IO=normal" "cd $W && perf record -F 499 -g -o $W/perf-normal.data -- $APP fuse $W/out-e 4 $SRC" > $W/run-e.log 2>&1; grep "^fuse" $W/run-e.log
rm -rf $W/out-e
echo runs-done
