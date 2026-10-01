#!/bin/bash
set -uo pipefail
log() { echo "[$(date -u +%T)] $*"; }
/work/setup.sh || { log setup-failed; exit 1; }
for t in integ prod; do
  docker exec vbuild bash -lc "cd /work/$t && ./bootstrap.sh default > /work/logs/bootstrap-$t.log 2>&1" || { log bootstrap-$t-failed; tail -30 /work/logs/bootstrap-$t.log; exit 1; }
  log bootstrapped-$t
done
/work/build.sh integ 36 > /work/logs/build-integ.out 2>&1 & P1=$!
/work/build.sh prod 36 > /work/logs/build-prod.out 2>&1 & P2=$!
wait $P1; R1=$?; wait $P2; R2=$?
cat /work/logs/build-integ.out /work/logs/build-prod.out
log "build exits integ=$R1 prod=$R2"
[ $R1 = 0 ] && /work/test.sh integ
[ $R2 = 0 ] && /work/test.sh prod
log pipeline-done
