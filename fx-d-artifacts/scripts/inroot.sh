#!/bin/bash
# usage: inroot.sh '<command>'   runs a login shell inside the build rootfs
exec chroot /work/rootfs /bin/bash -lc "$1"
