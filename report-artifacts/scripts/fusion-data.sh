#!/bin/bash
# Builds 32 batch indexes (65k English WET docs each, dense docids), fuses them into the large index,
# then builds 8 small indexes (2500 re-fed docids from the large range + 2500 new docids each).
set -uo pipefail
APP=/work/vespa/build/searchlib/src/tests/diskindex/fusion/searchlib_fusion_profile_app
W=/work/fusion
mkdir -p $W/ids
ls /work/wet/*.warc.wet.gz | sort > $W/wet-files.txt
python3 - <<PY
import random
files=[l.strip() for l in open("$W/wet-files.txt")]
B=32; N=65000
for b in range(B):
    open(f"$W/ids/batch{b:02d}.txt","w").write("".join(f"{d}\n" for d in range(b*N+1,(b+1)*N+1)))
    open(f"$W/ids/batch{b:02d}.files","w").write(" ".join(files[b*8:(b+1)*8]))
rng=random.Random(42)
large_end=B*N
refed=rng.sample(range(1,large_end+1),8*2500)
for j in range(8):
    ids=sorted(refed[j*2500:(j+1)*2500]+list(range(large_end+1+j*2500,large_end+1+(j+1)*2500)))
    open(f"$W/ids/small{j}.txt","w").write("".join(f"{d}\n" for d in ids))
    open(f"$W/ids/small{j}.files","w").write(" ".join(files[256+j*2:256+j*2+2]))
PY
T0=$(date +%s)
for b in $(seq -w 0 31); do
  echo "$APP build $W/batch$b $W/ids/batch$b.txt \$(cat $W/ids/batch$b.files)"
done > $W/build-batches.cmds
docker exec vbuild bash -lc "cd $W && cat build-batches.cmds | xargs -P 32 -I{} sh -c '{}'" > $W/build-batches.log 2>&1
echo "batches built in $(( $(date +%s) - T0 ))s"; grep -c "^build" $W/build-batches.log
for j in $(seq 0 7); do
  echo "$APP build $W/small$j $W/ids/small$j.txt \$(cat $W/ids/small$j.files)"
done > $W/build-small.cmds
docker exec vbuild bash -lc "cd $W && cat build-small.cmds | xargs -P 8 -I{} sh -c '{}'" > $W/build-small.log 2>&1
grep -c "^build" $W/build-small.log
T1=$(date +%s)
docker exec vbuild bash -lc "$APP fuse $W/large 4 $(for b in $(seq -w 0 31); do echo -n "$W/batch$b "; done)" > $W/fuse-large.log 2>&1
echo "large fused in $(( $(date +%s) - T1 ))s"; cat $W/fuse-large.log
