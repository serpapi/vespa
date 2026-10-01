#!/bin/bash
set -uo pipefail
log() { echo "[$(date -u +%T)] $*"; }
docker rm -f vbuild >/dev/null 2>&1
docker run -d --name vbuild --network host -v /work:/work -v /work/m2:/root/.m2 -w /work docker.io/vespaengine/vespa-build-almalinux-8:latest sleep infinity >/dev/null
docker inspect --format '{{index .RepoDigests 0}}' docker.io/vespaengine/vespa-build-almalinux-8:latest > /work/results/toolchain.txt
docker exec vbuild bash -lc 'g++ --version | head -1; cmake --version | head -1' >> /work/results/toolchain.txt
log container-up
docker exec vbuild bash -lc "cd /work/vespa && ./bootstrap.sh default > /work/logs/bootstrap.log 2>&1 && cmake -S . -B build -DEXCLUDE_TESTS_FROM_ALL=TRUE > /work/logs/cmake.log 2>&1" || { log configure-failed; tail -30 /work/logs/bootstrap.log /work/logs/cmake.log; exit 1; }
log configured
docker exec vbuild bash -lc "cd /work/vespa/build && make -j64 searchlib_sourceblender_test_app searchlib_sourceblender_benchmark_app > /work/logs/make-base.log 2>&1" || { log make-base-failed; grep -E 'error|Error' /work/logs/make-base.log | head -40; exit 1; }
log built-base
