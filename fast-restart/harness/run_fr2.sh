#!/bin/bash
# Follow-up run on a fresh i8g node · spec: fast-restart/PREREG.md (amendment 2026-11-08)
# After setup.sh and the 50M load: snapshot; the fusion A/B with queries for the full-fusion stall fix
# (stock, stock + async index removal, tiered + async index removal); then the three H4 arms.
set -uo pipefail
cd ~/harness
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
until [ -f /mnt/nvme/out/load.end ]; do sleep 30; done
STEPS=${STEPS:-"snapshot ab h4"}
for step in $STEPS; do
case $step in
snapshot)
  log "snapshot start"; ./arms.sh snapshot >> /mnt/nvme/out/progress.log 2>&1; log "snapshot done" ;;
ab)
  for arm in "ab-stock-q2 stock" "ab-fix-q fix" "ab-fixtiered-q h4 override VESPA_TIERED_FUSION 1"; do
    set -- $arm; name=$1 build=$2; shift 2; envl="$*"
    log "arm $name"; ./arms.sh arm $name $build "$envl" >> /mnt/nvme/out/progress.log 2>&1
    ~/harness/bin/r6load query -qps ${QPS:-50} -dur 3h -seed 7 -out /mnt/nvme/out/$name/queries.csv & Q=$!
    ./arms.sh feed $name ${AB_OPS:-30000000} >> /mnt/nvme/out/progress.log 2>&1
    kill -INT $Q; sleep 15; kill $Q 2>/dev/null
    ./arms.sh check $name >> /mnt/nvme/out/progress.log 2>&1
    log "arm $name done"
  done ;;
h4)
  log "h4 stock"; ./h4.sh h4-stock stock app >> /mnt/nvme/out/progress.log 2>&1 || log "h4-stock FAILED"
  log "h4 stock tls1g"; ./h4.sh h4-stock-tls1g stock app-h4-tls1g >> /mnt/nvme/out/progress.log 2>&1 || log "h4-stock-tls1g FAILED"
  log "h4 proto"; ./h4.sh h4-proto h4 app-h4-tls1g "override VESPA_TIERED_FUSION 1
override VESPA_DELTA_ATTRIBUTE_CHECKPOINTS 1" >> /mnt/nvme/out/progress.log 2>&1 || log "h4-proto FAILED"
  ./arms.sh check h4-proto >> /mnt/nvme/out/progress.log 2>&1 ;;
esac
done
log "FR2 DONE"
