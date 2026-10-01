#!/bin/bash
set -uo pipefail
for adv in normal sequential random; do
  sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2
  sudo systemd-run --quiet --scope -p MemoryMax=2G -p MemorySwapMax=0 /work/madv/madv_hot_test /work/madv/data $adv 24 > /work/madv/result4-$adv.txt 2>&1
  echo "== $adv"; tail -1 /work/madv/result4-$adv.txt
done
echo madv-done
