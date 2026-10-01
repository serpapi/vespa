#!/bin/bash
# usage: test.sh <integ|prod>
set -uo pipefail
T=$1
NAMES=$(sed -n 2p /work/results/targets-$T.txt | tr ' ' '|')
docker exec vbuild bash -lc "cd /work/$T/build && ctest -N -R '^($NAMES)\$' | tail -1; ctest -j 32 -R '^($NAMES)\$' --output-on-failure > /work/results/ctest-$T.txt 2>&1"; rc=$?
echo "[$(date -u +%T)] $T ctest exit=$rc"
grep -E "tests passed|tests failed|Total Test time" /work/results/ctest-$T.txt
grep -E "\*\*\*Failed|Not Run|\*\*\*Timeout|\*\*\*Exception" /work/results/ctest-$T.txt | head -20
exit $rc
