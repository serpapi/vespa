#!/bin/bash
set -euxo pipefail
# dead-man switch: the instance terminates itself after 10h even if nobody cleans up
shutdown -h +600 || true
dnf install -y docker git tmux htop rsync perf sysstat
systemctl enable --now docker
usermod -aG docker ec2-user
mkdir -p /work/m2 /work/logs /work/results && chown -R ec2-user:ec2-user /work
sudo -u ec2-user bash -c 'cd /work && git init -q vespa && cd vespa && git remote add origin https://github.com/vespa-engine/vespa.git && git fetch -q --depth 1 origin b135ebe18c7d8068ea5060e9c6a3a44d57f2a555 && git checkout -q FETCH_HEAD && git checkout -q -b base'
docker pull -q docker.io/vespaengine/vespa-build-almalinux-8:latest
touch /work/userdata-done
