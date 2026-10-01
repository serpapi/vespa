#!/bin/bash
# usage: perfstat.sh <tag> <impl> <benchmark args...>; counts only the timed loops on cpu 8
set -uo pipefail
TAG=$1; IMPL=$2; shift 2
mkdir -p /work/perf
[ -p /work/perf/ctl ] || mkfifo -m 666 /work/perf/ctl
[ -p /work/perf/ack ] || mkfifo -m 666 /work/perf/ack
EV='cycles,instructions,branch-misses,cpu/event=0x64,umask=0xf8,name=l1d_miss/,cpu/event=0x64,umask=0x08,name=l2_miss/'
sudo perf stat -C 8 -D -1 --control fifo:/work/perf/ctl,/work/perf/ack -x, -o /work/results/perfstat-$TAG-$IMPL.csv -e "$EV" -- sleep 100000 > /dev/null 2>&1 &
SP=$!
sleep 1
PP=$(pgrep -P $(pgrep -P $SP))
docker exec -e PERF_CTL_FIFO=/work/perf/ctl -e PERF_ACK_FIFO=/work/perf/ack vbuild bash -lc "cd /work/bench/perf && taskset -c 8 ./searchlib_sourceblender_benchmark_app --dir=/work/bench/perf/idx-$TAG --impl=$IMPL $* > /work/results/perfstat-$TAG-$IMPL.txt 2>&1"
rc=$?
sudo kill $PP
wait $SP
echo "[$(date -u +%T)] perfstat $TAG $IMPL exit=$rc"
exit $rc
