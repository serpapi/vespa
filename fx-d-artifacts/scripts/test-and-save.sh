#!/bin/bash
# In the VM rootfs: run the affected tests for the current build and save this variant's library and binaries.
set -uo pipefail
V=$1
cd /work/build
mkdir -p /work/results /work/variants/$V
ctest -R '^(searchlib_sourceselector_test_app|searchlib_sourceblender_test_app|searchlib_attribute_test_app|searchlib_extendattribute_test_app|searchlib_queryeval_profiled_iterator_test_app|searchcore_fusionrunner_test_app|searchcore_indexmanager_test_app|searchcore_indexcollection_test_app|searchcore_diskindexcleaner_test_app)$' --output-on-failure -j6 > /work/results/ctest-$V.txt 2>&1
echo "ctest-exit=$?"
tail -15 /work/results/ctest-$V.txt
cp searchlib/src/vespa/searchlib/libvespa_searchlib.so /work/variants/$V/
cp searchlib/src/tests/attribute/sourceselector/searchlib_sourceselector_clone_benchmark_app searchlib/src/tests/attribute/sourceselector/searchlib_sourceselector_test_app /work/variants/$V/
(cd /work/src/vespa && git diff HEAD --stat > /work/variants/$V/source-diff.txt; git diff HEAD | sha256sum > /work/variants/$V/source-diff.sha256)
sha256sum /work/variants/$V/* | tee /work/results/sha256-$V.txt
