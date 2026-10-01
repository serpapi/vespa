#!/bin/bash
# usage: fusion-variant.sh <name> <docker env args...>; same docs/docids as /work/fusion, different index settings
set -uo pipefail
NAME=$1; shift
ENVS="$*"
APP=/work/vespa/build/searchlib/src/tests/diskindex/fusion/searchlib_fusion_profile_app
B=/work/fusion
W=/work/fusion-$NAME
mkdir -p $W
for b in $(seq -w 0 31); do echo "$APP build $W/batch$b $B/ids/batch$b.txt \$(cat $B/ids/batch$b.files)"; done > $W/build-batches.cmds
for j in $(seq 0 7); do echo "$APP build $W/small$j $B/ids/small$j.txt \$(cat $B/ids/small$j.files)"; done > $W/build-small.cmds
docker exec $ENVS vperf bash -lc "cd $W && cat build-batches.cmds build-small.cmds | xargs -P 40 -I{} sh -c '{}'" > $W/build.log 2>&1
docker exec $ENVS vperf bash -lc "$APP fuse $W/large 4 $(for b in $(seq -w 0 31); do echo -n "$W/batch$b "; done)" > $W/fuse-large.log 2>&1
docker exec vperf bash -lc "rm -rf $W/batch*"
SRC="$W/large $(for j in 0 1 2 3 4 5 6 7; do echo -n "$W/small$j "; done)"
EV='cycles,instructions,cpu/event=0x24,umask=0x02,name=stli_other/,branch-misses'
for r in 1 2; do
  docker exec -e FUSION_IO=normal $ENVS vperf bash -lc "perf stat -e '$EV' -o $W/perfstat-$r.txt -- $APP fuse $W/out 4 $SRC" > $W/run-$r.log 2>&1
done
grep "^fuse" $W/fuse-large.log $W/run-2.log | sed 's/in_bytes.*out_bytes/.../'; grep -E "cycles|instructions|stli|branch" $W/perfstat-2.txt
docker exec vbuild bash -lc "/work/vespa/build/searchlib/src/apps/vespa-index-inspect/vespa-index-inspect dumpwords --indexdir $W/large --field body --verbose 2>/dev/null | awk -F'\t' '{n++; d+=\$2; b+=\$3} END {print \"words\", n, \"postings\", d, \"bits\", b}'"
