#!/bin/bash
# In the VM rootfs: put /work/src/vespa at variant A, B or C (all include the bench program).
set -euo pipefail
cd /work/src/vespa
git checkout -q -f 00ddfd8 && git clean -qfd -e configgen/target -e dist/vtag.map
git apply /work/bench.patch
case "$1" in
  A) ;;
  B) git apply /work/B.patch ;;
  C) git apply /work/B.patch && git apply /work/C.patch ;;
  *) echo "bad variant"; exit 1 ;;
esac
git status --short
