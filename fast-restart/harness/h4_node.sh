#!/bin/bash
# One H4 arm on its own i8g.4xlarge: setup, restore the 50M snapshot box A uploaded, run the arm.
# usage: h4_node.sh NAME BUILD APP [ENV]
set -uo pipefail
cd ~/harness
mkdir -p /mnt/nvme/out 2>/dev/null || true
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
sh setup.sh > /tmp/setup.log 2>&1 || { log "setup FAILED"; exit 1; }
log "setup done"
B=s3://si-greenfield-bench-524519922011/vespa-fork-restart/snap
until aws s3 ls $B/fr2-50m.ready > /dev/null 2>&1; do sleep 20; done
sudo docker rm -f vespa > /dev/null 2>&1 || true
log "snapshot download start"
aws s3 cp $B/fr2-50m.tar - --quiet | sudo tar -C /mnt/nvme/vespa -xpf - || { log "snapshot download FAILED"; exit 1; }
log "snapshot download done $(sudo du -sh /mnt/nvme/vespa/var.snap | cut -f1)"
log "h4 $1"
./h4.sh "$@" >> /mnt/nvme/out/progress.log 2>&1 || log "$1 FAILED"
[ "$1" = h4-proto ] && ./arms.sh check h4-proto >> /mnt/nvme/out/progress.log 2>&1
log "H4 NODE DONE"
