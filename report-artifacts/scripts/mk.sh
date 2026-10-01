#!/bin/bash
# usage: mk.sh <logname> <targets...>
set -uo pipefail
L=$1; shift
docker exec vbuild bash -lc "cd /work/vespa/build && make -j64 $* > /work/logs/$L.log 2>&1"; rc=$?
echo "[$(date -u +%T)] make $L exit=$rc"
grep -E "error:|warning:" /work/logs/$L.log | sort | uniq | head -30
exit $rc
