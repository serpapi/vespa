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
T0=$(cat $OUT/feed.start); max=0
while true; do
  sleep 5
  now=$(date +%s.%N); el=$(echo "$now - $T0" | bc | cut -d. -f1)
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
sudo kill -INT $BPF; wait $BPF || true
for f in "${FEEDERS[@]}"; do pkill -P $f 2>/dev/null || true; kill $f 2>/dev/null || true; done
pkill -f 'r6load docs' || true; pkill -f 'throttle.py' || true; pkill -f 'vespa feed' || true
until awk -F'\t' -v t=$T_EXIT '$1>t && $4=="searchnode" && $7 ~ /started\/1 name="proton"/ {f=1} END{exit !f}' $LOG 2>/dev/null; do sleep 2; done
kill $IOSTAT
python3 ~/harness/events.py $LOG $T_STOP $T_EXIT "$NAME" kill 0 | tee $OUT/restart.json
