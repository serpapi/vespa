#!/bin/bash
set -uo pipefail
for docs in 50000000 460000000; do
  if [ $docs = 50000000 ]; then L=5000000; else L=5900000; fi
  for k in 1 10; do for u in 0 1; do for impl in legacy current; do
    /work/perfstat.sh d${docs}-k${k}-s0.02-u${u} $impl --children=zc --docs=$docs --large=$L --small=5000 --k=$k --stale=0.02 --unpack=$u --reps=5
  done; done; done
done
echo matrix-done
