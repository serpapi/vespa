#!/bin/bash
# H4: restart after SIGKILL at the worst point of a steady feed, and bytes written · spec: fast-restart/PREREG.md
# (H4 and the amendment of 2026-11-08)
# usage: ./h4.sh NAME BUILD APP [ENV]
#   Restores the 50M snapshot, starts BUILD (stock | an artifacts/ name) with ENV in default-env.txt, deploys APP
#   (a directory in ~/harness), re-puts documents at RATE docs/s, and SIGKILLs proton at the worst point: after
#   MIN_S seconds of feed, at the first 5 s poll where the transaction log directory is at least 90 % of the
#   largest size seen in this feed (or at MAX_S). bpftrace sums proton's write bytes by file path meanwhile.
#   Then waits for proton to start again and summarises the restart with events.py.
set -euo pipefail
NAME=$1 BUILD=$2 APP=$3 ENVL=${4:-}
RATE=${RATE:-5000} MIN_S=${MIN_S:-3000} MAX_S=${MAX_S:-4200}
V=/mnt/nvme/vespa; OUT=/mnt/nvme/out/$NAME; LOG=$V/logs/vespa/vespa.log
cd ~/harness
proton_pid() { pgrep -f '^/opt/vespa/sbin/vespa-proton-bin' | head -1; }
tls_dir() { sudo find $V/var/db/vespa/search -maxdepth 4 -type d -name tls | head -1; }

./arms.sh arm $NAME $BUILD "$ENVL"
vespa deploy --wait 600 ~/harness/$APP > $OUT/deploy.txt 2>&1
sleep 60 # let the deployed config reach proton
pid=$(proton_pid); TLS=$(tls_dir)
sudo cat /proc/$pid/io > $OUT/io.before
sudo bpftrace ~/harness/h4-writes.bt $pid > $OUT/writes.txt 2> $OUT/bpftrace.err & BPF=$!
sleep 5
date +%s.%N > $OUT/feed.start
iostat -dxm 30 > $OUT/iostat.txt & IOSTAT=$!
FEEDERS=()
P=8; per_s=$(( RATE / P )); n=$(( per_s * MAX_S + per_s * 60 ))
for i in $(seq 0 $((P - 1))); do
  start=$(( (i * 6100000 + 1234567) % (50000000 - n) ))
  ( ~/harness/bin/r6load docs -start $start -n $n | python3 ~/harness/throttle.py $per_s \
      | vespa feed --connections 4 - > /dev/null 2> $OUT/feed-$i.err ) &
  FEEDERS+=($!)
done
# Deterministic partial updates of three plain numeric attributes on ids the re-puts above never touch.
UPD_START=48000000 UPD_N=${UPD_N:-2000000}
( python3 ~/harness/updates.py $UPD_START $UPD_N | python3 ~/harness/throttle.py ${UPD_RATE:-1000} \
    | vespa feed --connections 2 - > /dev/null 2> $OUT/feed-updates.err ) &
FEEDERS+=($!)
T0=$(cat $OUT/feed.start); max=0
while true; do
  sleep 5
  el=$(awk -v a=$(date +%s.%N) -v b=$T0 'BEGIN { printf "%d", a - b }')
  size=$(sudo du -sb $TLS | cut -f1)
  echo "$el $size" >> $OUT/tls-size.txt
  [ $size -gt $max ] && max=$size
  if [ $el -ge $MAX_S ] || { [ $el -ge $MIN_S ] && [ $size -ge $(( max * 9 / 10 )) ]; }; then break; fi
done
sudo cat /proc/$pid/io > $OUT/io.kill
T_STOP=$(date +%s.%N)
sudo kill -9 $pid
while sudo kill -0 $pid 2>/dev/null; do sleep 0.2; done
T_EXIT=$(date +%s.%N)
echo "$T_STOP $T_EXIT $el $size $max" > $OUT/kill.txt
for f in "${FEEDERS[@]}"; do pkill -P $f 2>/dev/null || true; kill $f 2>/dev/null || true; done
pkill -f 'r6load docs' || true; pkill -f 'updates.py' || true; pkill -f 'throttle.py' || true
pkill -f 'vespa feed' || true
sudo kill -INT $BPF; wait $BPF || true
until awk -F'\t' -v t=$T_EXIT '$1>t && $4=="searchnode" && $7 ~ /started\/1 name="proton"/ {f=1} END{exit !f}' $LOG 2>/dev/null; do sleep 2; done
kill $IOSTAT
python3 ~/harness/events.py $LOG $T_STOP $T_EXIT "$NAME" kill 0 | tee $OUT/restart.json
# Documents and the partial updates are functions of the id, so after any number of re-puts and at least one pass
# of updates every arm must return the same counts for filters on the plain numeric attributes (restored from
# delta checkpoints in h4-proto) and match-all.
until curl -sf 'http://localhost:8080/search/?yql=select%20*%20from%20webpage%20where%20true&hits=0' | grep -q totalCount; do sleep 5; done
for q in true "url_len > 60" "path_depth = 2" "status_code = 1" "quality_score > 0.5" "last_updated > 1750000000" \
         "original_authority = 7" "host_pagerank < 10" "host_quality > 0.9" "tranco_rank < 1000" "price > 900" \
         "status = 3" "boost < 0.01" "price >= 1000" "status = 7" "boost >= 2"; do
  c=$(curl -s --data-urlencode "yql=select * from webpage where $q" --data-urlencode hits=0 --data-urlencode ranking=unranked \
      --data-urlencode timeout=60s -G http://localhost:8080/search/ | jq .root.fields.totalCount)
  echo "$c	$q"
done | tee $OUT/attribute-hits.txt
