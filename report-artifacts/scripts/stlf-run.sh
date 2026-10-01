#!/bin/bash
# usage: stlf-run.sh <tag>; buffered-IO fusion of large+8 small under perf stat with store-forwarding events
set -uo pipefail
TAG=$1
APP=/work/vespa/build/searchlib/src/tests/diskindex/fusion/searchlib_fusion_profile_app
W=/work/fusion
SRC="$W/large $(for j in 0 1 2 3 4 5 6 7; do echo -n "$W/small$j "; done)"
EV='cycles,instructions,cpu/event=0x24,umask=0x02,name=stli_other/,cpu/event=0x35,name=stlf/,branch-misses'
docker exec -e FUSION_IO=normal vperf bash -lc "perf stat -e '$EV' -o $W/perfstat-stlf-$TAG.txt -- $APP fuse $W/out-$TAG 4 $SRC" > $W/run-stlf-$TAG.log 2>&1
grep "^fuse" $W/run-stlf-$TAG.log | sed 's/.*wall_s/wall_s/'; grep -E "cycles|instructions|stli|stlf|branch" $W/perfstat-stlf-$TAG.txt
