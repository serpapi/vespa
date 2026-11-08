#!/bin/bash
# Extra restart arms after run_all.sh · spec: fast-restart/PREREG.md (amendment: initializer threads)
set -uo pipefail
cd ~/harness
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
until grep -q "ALL DONE" /mnt/nvme/out/progress.log; do sleep 30; done
./arms.sh arm init16 stock >> /mnt/nvme/out/progress.log 2>&1
vespa deploy --wait 600 ~/harness/app-init16 >> /mnt/nvme/out/progress.log 2>&1
sleep 30
for spec in "init16-k0-0 kill 0" "init16-k0-1 kill 0" "init16-k0-2 kill 0" "init16-k5m-1 kill 5000000"; do
  log "restart $spec"; ./restart.sh $spec >> /mnt/nvme/out/progress.log 2>&1 || log "restart $spec FAILED"
done
log "EXTRA DONE"
