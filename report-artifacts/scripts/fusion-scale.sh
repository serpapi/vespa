#!/bin/bash
# N concurrent single-field fusions (buffered IO, inputs in page cache, outputs on tmpfs):
# proxy for a word-range-parallel merge of one field.
set -uo pipefail
APP=/work/vespa/build/searchlib/src/tests/diskindex/fusion/searchlib_fusion_profile_app
W=/work/fusion
IMG=docker.io/vespaengine/vespa-build-almalinux-8:latest
[ -d $W/medium ] || docker exec vperf bash -lc "$APP fuse $W/medium 4 $(for b in 00 01 02 03 04 05 06 07; do echo -n "$W/batch$b "; done)" > $W/fuse-medium.log 2>&1
cat $W/fuse-medium.log
SRC="$W/medium $(for j in 0 1 2 3 4 5 6 7; do echo -n "$W/small$j "; done)"
docker run --rm -e FUSION_IO=normal -v /work:/work -v /dev/shm:/shm $IMG bash -lc "$APP fuse /shm/warm 1 $SRC" > /dev/null 2>&1
sudo rm -rf /dev/shm/*
for N in 1 2 4 8 16 24; do
  docker run --rm -e FUSION_IO=normal -v /work:/work -v /dev/shm:/shm $IMG bash -lc "
    s=\$(date +%s.%N)
    for i in \$(seq 1 $N); do $APP fuse /shm/out-\$i 1 $SRC > /work/fusion/scale-$N-\$i.log 2>&1 & done
    wait
    e=\$(date +%s.%N)
    python3 -c \"
import re,glob
runs=[dict(re.findall(r'(\\\\w+)=([\\\\d.]+)',open(f).read())) for f in glob.glob('/work/fusion/scale-$N-*.log')]
inb=sum(float(r['in_bytes']) for r in runs); cpu=sum(float(r['cpu_s']) for r in runs); wall=\$e-\$s
print('N=$N runs=%d wall_s=%.1f aggregate_in_MBps=%.0f per_run_in_MBps_cpu=%.1f' % (len(runs), wall, inb/wall/1e6, inb/cpu/1e6))
\""
  sudo rm -rf /dev/shm/*
done
echo scale-done
