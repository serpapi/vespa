#!/bin/bash
# Runs as root in the lima VM: unpack the flattened vespa-build image and the base source, ready for chroot builds.
set -euo pipefail
R=/work/rootfs
mkdir -p $R /work/src
if [ ! -x $R/bin/bash ]; then tar -xf /tmp/vm/vespa-build-rootfs.tar -C $R; fi
if [ ! -d /work/src/vespa ]; then tar -xzf /tmp/vm/vespa-8ec58e1230.tar.gz -C /work/src; fi
cp /etc/resolv.conf $R/etc/resolv.conf
mkdir -p $R/work $R/root/.m2
for d in proc sys dev dev/pts; do mountpoint -q $R/$d || mount --bind /$d $R/$d; done
mountpoint -q $R/work || mount --bind /work $R/work
if [ -d /tmp/vm/m2 ] && [ ! -e $R/root/.m2/.seeded ]; then cp -a /tmp/vm/m2/. $R/root/.m2/ && touch $R/root/.m2/.seeded; fi
echo setup-ok
