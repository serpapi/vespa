#!/bin/bash
set -euxo pipefail
# dead-man switch: the instance terminates itself after 6h even if nobody cleans up
shutdown -h +360 || true
dnf install -y docker git tmux htop rsync perf sysstat
systemctl enable --now docker
usermod -aG docker ec2-user
mkdir -p /work/logs /work/results && chown -R ec2-user:ec2-user /work
sudo -u ec2-user bash -c 'cd /work && git init -q vespa && cd vespa && git remote add origin https://github.com/vespa-engine/vespa.git && git fetch -q --depth 1 origin b135ebe18c7d8068ea5060e9c6a3a44d57f2a555 a102426624bc600d09dd073102ed59557fa69ebd && git checkout -q --detach b135ebe18c7d8068ea5060e9c6a3a44d57f2a555'
docker pull -q docker.io/vespaengine/vespa-build-almalinux-8@sha256:9bf771f1d3a349bb1749f6f3c5cade72d05fe14e1476a9915b272068def2c864
touch /work/userdata-done
