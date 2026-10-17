#!/bin/bash
# In the VM rootfs: alternate variants, one measured phase per fresh process, pinned to vCPU 2.
set -uo pipefail
N=${N:-10}; DOCS=${DOCS:-590000000}
OUT=/work/results/bench-$(date -u +%Y%m%dT%H%M%SZ).txt
echo "# start $(date -u +%FT%TZ) N=$N DOCS=$DOCS nproc=$(nproc) loadavg=$(cat /proc/loadavg)" | tee $OUT
mkdir -p /work/benchdir && cd /work/benchdir
for i in $(seq 1 $N); do
  for phase in clone ctor; do
    for v in A B C0 C; do
      line=$(LD_LIBRARY_PATH=/work/variants/$v taskset -c 2 /work/variants/$v/searchlib_sourceselector_clone_benchmark_app bench $phase $DOCS 1 2>&1 | grep ^phase=)
      echo "rep=$i variant=$v $line" | tee -a $OUT
    done
  done
  line=$(LD_LIBRARY_PATH=/work/variants/A taskset -c 2 /work/variants/A/searchlib_sourceselector_clone_benchmark_app bench grow $DOCS 2>&1 | grep ^phase=)
  echo "rep=$i variant=A $line" | tee -a $OUT
done
echo "# end $(date -u +%FT%TZ) loadavg=$(cat /proc/loadavg)" | tee -a $OUT
