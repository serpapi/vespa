#!/bin/bash
# Fast-restart measurement sequence on the i8g node · spec: fast-restart/PREREG.md
# Runs after setup.sh and the 50M load. Each step appends to /mnt/nvme/out/progress.log.
set -uo pipefail
cd ~/harness
cp /tmp/r6load-new bin/r6load 2>/dev/null || true
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
STEPS=${STEPS:-"snapshot restarts replay ab"}
for step in $STEPS; do
case $step in
snapshot)
  log "snapshot start"; ./arms.sh snapshot >> /mnt/nvme/out/progress.log 2>&1; log "snapshot done" ;;
restarts)
  ./arms.sh arm restarts stock >> /mnt/nvme/out/progress.log 2>&1
  for spec in "stock-k0-1 kill 0" "stock-k5m-perf kill 5000000 60" "stock-k5m-2 kill 5000000" \
              "stock-c2m clean 2000000" "stock-k0-2 kill 0" "stock-k5m-3 kill 5000000"; do
    log "restart $spec"; ./restart.sh $spec >> /mnt/nvme/out/progress.log 2>&1 || log "restart $spec FAILED"
  done ;;
replay)
  ./arms.sh arm replay proto "override VESPA_REPLAY_DECODE_THREADS 8" >> /mnt/nvme/out/progress.log 2>&1
  for spec in "proto8-k5m-perf kill 5000000 60" "proto8-k5m-2 kill 5000000" "proto8-k5m-3 kill 5000000" "proto8-k0-1 kill 0"; do
    log "restart $spec"; ./restart.sh $spec >> /mnt/nvme/out/progress.log 2>&1 || log "restart $spec FAILED"
  done ;;
ab)
  for arm in "ab-stock stock" "ab-tiered proto override VESPA_TIERED_FUSION 1"; do
    set -- $arm; name=$1 build=$2; shift 2; envl="$*"
    log "arm $name"; ./arms.sh arm $name $build "$envl" >> /mnt/nvme/out/progress.log 2>&1
    ./arms.sh feed $name ${AB_OPS:-30000000} >> /mnt/nvme/out/progress.log 2>&1
    ./arms.sh check $name >> /mnt/nvme/out/progress.log 2>&1
    log "arm $name done"
  done ;;
esac
done
log "ALL DONE"
