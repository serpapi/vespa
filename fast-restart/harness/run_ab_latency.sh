#!/bin/bash
# Fusion A/B repeat with an open-loop query load during the feed · spec: fast-restart/PREREG.md H5 (latency guard)
set -uo pipefail
cd ~/harness
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
until grep -q "EXTRA DONE" /mnt/nvme/out/progress.log; do sleep 30; done
for arm in "ab-stock-q stock" "ab-tiered-q proto override VESPA_TIERED_FUSION 1"; do
  set -- $arm; name=$1 build=$2; shift 2; envl="$*"
  log "arm $name"; ./arms.sh arm $name $build "$envl" >> /mnt/nvme/out/progress.log 2>&1
  ~/harness/bin/r6load query -qps ${QPS:-50} -dur 3h -seed 7 -out /mnt/nvme/out/$name/queries.csv & Q=$!
  ./arms.sh feed $name ${AB_OPS:-30000000} >> /mnt/nvme/out/progress.log 2>&1
  kill -INT $Q; sleep 15; kill $Q 2>/dev/null
  ./arms.sh check $name >> /mnt/nvme/out/progress.log 2>&1
  log "arm $name done"
done
log "AB LATENCY DONE"
