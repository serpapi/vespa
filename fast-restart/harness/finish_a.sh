#!/bin/bash
# Box A after the switch to parallel H4 nodes: finish ab-fix-q exactly as run_fr2.sh would, upload the 50M snapshot
# for the H4 nodes, then run ab-fixtiered-q. FEED and Q are the PIDs of the running ab-fix-q feed and query client.
set -uo pipefail
FEED=$1 Q=$2
cd ~/harness
log() { echo "$(date -u +%FT%TZ) $*" | tee -a /mnt/nvme/out/progress.log; }
while kill -0 $FEED 2>/dev/null; do sleep 10; done
kill -INT $Q; sleep 15; kill $Q 2>/dev/null
./arms.sh check ab-fix-q >> /mnt/nvme/out/progress.log 2>&1
log "arm ab-fix-q done"
sudo docker rm -f vespa > /dev/null 2>&1 || true
log "snapshot upload start"
sudo tar -C /mnt/nvme/vespa -cf - var.snap | aws s3 cp - s3://si-greenfield-bench-524519922011/vespa-fork-restart/snap/fr2-50m.tar --expected-size 80000000000 --quiet \
  && echo ok | aws s3 cp - s3://si-greenfield-bench-524519922011/vespa-fork-restart/snap/fr2-50m.ready --quiet \
  && log "snapshot upload done" || log "snapshot upload FAILED"
name=ab-fixtiered-q
log "arm $name"; ./arms.sh arm $name h4 "override VESPA_TIERED_FUSION 1" >> /mnt/nvme/out/progress.log 2>&1
~/harness/bin/r6load query -qps 50 -dur 3h -seed 7 -out /mnt/nvme/out/$name/queries.csv & Q=$!
./arms.sh feed $name 30000000 >> /mnt/nvme/out/progress.log 2>&1
kill -INT $Q; sleep 15; kill $Q 2>/dev/null
./arms.sh check $name >> /mnt/nvme/out/progress.log 2>&1
log "arm $name done"
log "FR2 DONE"
