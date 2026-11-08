#!/bin/bash
# One restart measurement · spec: fast-restart/PREREG.md
# usage: ./restart.sh TAG MODE OPS [PERF_SECONDS]
#   MODE kill:  flush everything, re-put OPS docs, SIGKILL proton (the sentinel restarts it).
#   MODE clean: flush everything, re-put OPS docs, `vespa-sentinel-cmd restart searchnode` (SIGTERM; with
#               flush-on-shutdown proton flushes before it exits).
# Prints one JSON line (events.py) and appends it to /mnt/nvme/out/restarts.jsonl.
set -euo pipefail
TAG=$1 MODE=$2 OPS=$3 PERF=${4:-0}
OUT=/mnt/nvme/out/$TAG; mkdir -p $OUT
LOG=/mnt/nvme/vespa/logs/vespa/vespa.log
dx() { sudo docker exec vespa "$@"; }
proton_pid() { pgrep -f '^/opt/vespa/sbin/vespa-proton-bin' | head -1; }
wait_started() { # wait for a "started/1 name=proton" event after unix time $1
  until awk -F'\t' -v t=$1 '$1>t && $4=="searchnode" && $7 ~ /started\/1 name="proton"/ {f=1} END{exit !f}' $LOG 2>/dev/null; do sleep 2; done
}
dx vespa-proton-cmd --local triggerFlush > $OUT/flush.txt 2>&1 || true
sleep 5
if [ "$OPS" -gt 0 ]; then
  START=$(( (RANDOM * 32768 + RANDOM) % (50000000 - OPS) ))
  /usr/bin/time -f %e -o $OUT/feed.seconds ~/harness/load.sh $OPS 8 $START
fi
iostat -dxm 5 > $OUT/iostat.txt & IOSTAT=$!
pid=$(proton_pid)
T_STOP=$(date +%s.%N)
if [ $MODE = kill ]; then
  sudo kill -9 $pid
else
  dx vespa-sentinel-cmd restart searchnode > $OUT/sentinel.txt 2>&1
fi
while sudo kill -0 $pid 2>/dev/null; do sleep 0.2; done
T_EXIT=$(date +%s.%N)
if [ "$PERF" -gt 0 ]; then
  until npid=$(proton_pid) && [ -n "$npid" ] && grep -q "transactionlog.replay.start.*webpage" <(awk -F'\t' -v t=$T_EXIT '$1>t' $LOG); do sleep 1; done
  sudo perf record -F 199 -g -p $npid -o $OUT/perf.data -- sleep $PERF > /dev/null 2>&1 || true
  sudo perf report -i $OUT/perf.data --no-children --sort comm --stdio 2>/dev/null | grep -v '^#' | grep . | head -30 > $OUT/perf-threads.txt || true
  sudo perf report -i $OUT/perf.data --no-children --sort comm,sym --percent-limit 0.5 --stdio -g none 2>/dev/null | grep -v '^#' | grep . | head -80 > $OUT/perf-top.txt || true
  sudo perf report -i $OUT/perf.data --children --sort sym --percent-limit 3 --stdio -g none 2>/dev/null | grep -v '^#' | grep . | head -60 > $OUT/perf-inclusive.txt || true
fi
wait_started $T_EXIT
kill $IOSTAT
python3 ~/harness/events.py $LOG $T_STOP $T_EXIT "$TAG" "$MODE" "$OPS" | tee -a /mnt/nvme/out/restarts.jsonl
