#!/bin/bash
set -euo pipefail
cd /work/vespa
git fetch -q /work/branches.bundle 'refs/heads/agents/*:refs/heads/agents/*'
git worktree add -q /work/integ agents/serpapi-vespa-perf
git worktree add -q /work/prod agents/serpapi-8.679.50
for t in integ prod; do echo "$t $(git -C /work/$t rev-parse HEAD) $(git -C /work/$t log -1 --format=%s | cut -c1-60)"; git -C /work/$t --no-pager log --format='  %h %s' -3 | cut -c1-90; done
IMG=docker.io/vespaengine/vespa-build-almalinux-8@sha256:9bf771f1d3a349bb1749f6f3c5cade72d05fe14e1476a9915b272068def2c864
mkdir -p /work/m2
docker rm -f vbuild >/dev/null 2>&1 || true
docker run -d --name vbuild --network host -v /work:/work -v /work/m2:/root/.m2 -w /work $IMG sleep infinity >/dev/null
docker exec vbuild bash -lc 'g++ --version | head -1; cmake --version | head -1'
