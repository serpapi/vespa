#!/bin/bash
# Regenerate variant patches from the branch and push them into the VM's /work.
set -euo pipefail
cd /Users/illia/Projects/vespa.worktrees/optimize-fusion-thread-performance
S=/Users/illia/.copilot/session-state/03c7f153-b704-472e-87f5-0692fd15d3ce/files/vm
B=$(git log --format=%H --grep='^\[searchlib\] Build a new source selector' -1)
C=$(git log --format=%H --grep='^\[searchlib\] Remap source selector values' -1)
H=$(git log --format=%H --grep='^\[searchlib\] Add a source selector clone dump' -1)
git diff $C $H > $S/bench.patch
git diff ${B}~1 $B > $S/B.patch
git diff ${C}~1 $C > $S/C.patch
echo "B=$B C=$C bench=$H" > $S/variants.txt
cat $S/variants.txt
limactl copy $S/bench.patch $S/B.patch $S/C.patch $S/variants.txt fxd-build:/tmp/vm/
limactl shell fxd-build -- sudo cp /tmp/vm/bench.patch /tmp/vm/B.patch /tmp/vm/C.patch /tmp/vm/variants.txt /work/
