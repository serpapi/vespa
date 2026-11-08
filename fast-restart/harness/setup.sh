#!/bin/sh
# Fast-restart harness · spec: fast-restart/PREREG.md
# Run on the i8g host as ec2-user from ~/harness (a copy of fast-restart/harness). Mounts the instance-store
# NVMe at /mnt/nvme, starts vespaengine/vespa:$VESPA with host networking and its data on the NVMe, installs
# the vespa CLI, builds r6load and deploys app/.
set -eux
VESPA=${VESPA:-8.781.11}
DEV=$(lsblk -dno NAME,MODEL | awk '/Instance Storage/{print "/dev/"$1; exit}')
if ! mountpoint -q /mnt/nvme; then
  sudo blkid "$DEV" || sudo mkfs.xfs -f "$DEV"
  sudo mkdir -p /mnt/nvme
  sudo mount -o noatime "$DEV" /mnt/nvme
  sudo chown ec2-user /mnt/nvme
fi
sudo dnf -y -q install docker sysstat golang jq perf bpftrace
sudo systemctl enable --now docker
sudo mkdir -p /mnt/nvme/vespa/var /mnt/nvme/vespa/logs /mnt/nvme/out
sudo chown -R 1000:1000 /mnt/nvme/vespa
sudo chown ec2-user /mnt/nvme/out
if ! sudo docker ps --format '{{.Names}}' | grep -qx vespa; then
  sudo docker run -d --name vespa --network host --privileged --ulimit nofile=262144:262144 --ulimit nproc=409600:409600 \
    ${EXTRA_DOCKER_ARGS:-} -v /mnt/nvme/vespa/var:/opt/vespa/var -v /mnt/nvme/vespa/logs:/opt/vespa/logs vespaengine/vespa:$VESPA
fi
if [ ! -x /usr/local/bin/vespa ]; then
  curl -sL https://github.com/vespa-engine/vespa/releases/download/v$VESPA/vespa-cli_${VESPA}_linux_arm64.tar.gz | tar xz -C /tmp
  sudo cp /tmp/vespa-cli_${VESPA}_linux_arm64/bin/vespa /usr/local/bin/vespa
fi
(cd ~/harness/r6load && go build -o ~/harness/bin/r6load .)
vespa config set target local
until curl -sf http://localhost:19071/state/v1/health >/dev/null; do sleep 3; done
vespa deploy --wait 600 ~/harness/app
vespa status --wait 300
