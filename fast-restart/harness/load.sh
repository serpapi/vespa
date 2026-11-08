#!/bin/sh
# Feeds docs [START, START+N) with P parallel `r6load docs | vespa feed -` pipelines. Re-feeding an existing
# range re-puts the same ids (the production re-crawl shape).
# usage: ./load.sh N [P] [START]
set -eu
N=$1 P=${2:-8} START=${3:-0}
per=$(( (N + P - 1) / P )); i=0
while [ $i -lt $P ]; do
  s=$(( START + i * per )); n=$per; [ $((s + n)) -gt $((START + N)) ] && n=$((START + N - s))
  ( ~/harness/bin/r6load docs -start $s -n $n | vespa feed --connections 4 - > /dev/null 2> /mnt/nvme/out/feed-$i.err ) &
  i=$((i + 1))
done
wait
