#!/bin/bash
# In the VM rootfs: B's tests (minus the init_docs API test) compiled against variant A.
set -uo pipefail
/work/variant.sh A >/dev/null
cd /work/src/vespa && git apply --include="*sourceselector_test.cpp" /work/B.patch
python3 - <<PY
p="searchlib/src/tests/attribute/sourceselector/sourceselector_test.cpp"
s=open(p).read(); a=s.index("TEST(SourceStoreTest"); b=s.index("}  // namespace",a)
open(p,"w").write(s[:a]+s[b:])
PY
cd /work/build && make -j12 searchlib_sourceselector_test_app > /work/make-A-newtests.log 2>&1 || { echo make-failed; exit 1; }
cd searchlib/src/tests/attribute/sourceselector && ./searchlib_sourceselector_test_app > /work/results/failing-first-on-A.txt 2>&1
echo "test-exit=$?"
grep -E "^\[  (FAILED|PASSED) |Expected|Which is|actual|^\[==" /work/results/failing-first-on-A.txt
