#!/bin/bash
# Fusion A/B arms · spec: fast-restart/PREREG.md H4/H5
# usage: ./arms.sh snapshot            stop vespa cleanly and reflink-copy its data to var.snap
#        ./arms.sh arm NAME BUILD [ENV] restore var.snap, start vespa with BUILD (stock | serpapi-8.781.11 | tiered),
#                                      ENV lines go to default-env.txt (e.g. "override VESPA_TIERED_FUSION 1")
#        ./arms.sh feed NAME OPS       re-put OPS docs in 2M chunks at fixed offsets, sampling disk indexes and I/O
#        ./arms.sh check NAME          hit counts of a fixed query set into out/NAME/hits.txt (rank profile
#                                      "unranked": the default profile caps total hits, ~12k)
set -euo pipefail
V=/mnt/nvme/vespa; OUT=/mnt/nvme/out; ART=/mnt/nvme/art
IMG=vespaengine/vespa:8.781.11
dx() { sudo docker exec vespa "$@"; }
proton_pid() { pgrep -f '^/opt/vespa/sbin/vespa-proton-bin' | head -1; }
index_dir() { ls -d $V/var/db/vespa/search/cluster.content/n0/documents/webpage/0.ready/index; }
start_vespa() { # extra docker args...
  sudo docker run -d --name vespa --network host --privileged --ulimit nofile=262144:262144 \
    --ulimit nproc=409600:409600 -v $V/var:/opt/vespa/var -v $V/logs:/opt/vespa/logs "$@" $IMG > /dev/null
  until curl -sf http://localhost:19071/state/v1/health > /dev/null; do sleep 3; done
  until curl -sf 'http://localhost:8080/search/?yql=select%20*%20from%20webpage%20where%20true&hits=0' | grep -q totalCount; do sleep 5; done
}
case $1 in
snapshot)
  dx vespa-proton-cmd --local triggerFlush || true
  sudo docker stop -t 900 vespa > /dev/null; sudo docker rm vespa > /dev/null
  sudo rm -rf $V/var.snap; sudo cp -a --reflink=always $V/var $V/var.snap
  echo "snapshot $(sudo du -sh $V/var.snap | cut -f1)" ;;
arm)
  NAME=$2 BUILD=$3 ENVL=${4:-}; mkdir -p $OUT/$NAME
  sudo docker rm -f vespa > /dev/null 2>&1 || true
  sudo rm -rf $V/var; sudo cp -a --reflink=always $V/var.snap $V/var
  ARGS=()
  if [ $BUILD != stock ]; then
    [ -d $ART/$BUILD ] || aws s3 cp --recursive --quiet s3://si-greenfield-bench-524519922011/vespa-fork-restart/artifacts/$BUILD $ART/$BUILD
    ARGS+=(-v $ART/$BUILD/libvespa_searchlib.so:/opt/vespa/lib64/libvespa_searchlib.so:ro)
    [ -f $ART/$BUILD/vespa-proton-bin ] && chmod +x $ART/$BUILD/vespa-proton-bin && ARGS+=(-v $ART/$BUILD/vespa-proton-bin:/opt/vespa/sbin/vespa-proton-bin:ro)
  fi
  sudo docker run --rm --entrypoint cat $IMG /opt/vespa/conf/vespa/default-env.txt > $OUT/$NAME/default-env.txt
  [ -n "$ENVL" ] && echo "$ENVL" >> $OUT/$NAME/default-env.txt
  ARGS+=(-v $OUT/$NAME/default-env.txt:/opt/vespa/conf/vespa/default-env.txt:ro)
  start_vespa "${ARGS[@]}"
  if [[ $NAME == ab-* ]]; then
    vespa deploy --wait 600 ~/harness/app-ab > $OUT/$NAME/deploy.txt 2>&1
    sleep 30
  fi
  echo "$NAME up: build=$BUILD searchlib=$(dx sha256sum /opt/vespa/lib64/libvespa_searchlib.so | cut -c1-12) proton=$(dx sha256sum /opt/vespa/sbin/vespa-proton-bin | cut -c1-12) env=[$(sudo cat /proc/$(proton_pid)/environ | tr '\0' '\n' | grep -E '^VESPA_(TIERED|REPLAY)' | tr '\n' ' ')]" | tee $OUT/$NAME/arm.txt
  if [ -n "$ENVL" ]; then grep -q "$(echo $ENVL | awk '{print $2}')" $OUT/$NAME/arm.txt || { echo "env not in proton" >&2; exit 1; }; fi ;;
feed)
  NAME=$2 OPS=$3; D=$OUT/$NAME; mkdir -p $D
  pid=$(proton_pid); idx=$(index_dir)
  sudo cat /proc/$pid/io > $D/io.before; date +%s.%N > $D/feed.start
  ( while true; do echo "$(date +%s) $(ls $idx | grep -c '^index\.') $(ls $idx | tr '\n' ' ')"; sleep 10; done > $D/sources.txt ) & SAMPLER=$!
  iostat -dxm 30 > $D/iostat.txt & IOSTAT=$!
  done_ops=0; i=0
  while [ $done_ops -lt $OPS ]; do
    start=$(( (i * 7368787) % 48000000 )); n=$(( OPS - done_ops < 2000000 ? OPS - done_ops : 2000000 ))
    ~/harness/load.sh $n 8 $start; done_ops=$((done_ops + n)); i=$((i + 1))
  done
  date +%s.%N > $D/feed.end
  # let a fusion that is still running finish (up to 30 min): every fusion.start since the feed began has its
  # fusion.complete
  T0=$(cat $D/feed.start)
  for _ in $(seq 180); do
    s=$(awk -F'\t' -v t=$T0 '$1>t && $7 ~ /name="fusion.start"/' $V/logs/vespa/vespa.log | grep -c webpage || true)
    c=$(awk -F'\t' -v t=$T0 '$1>t && $7 ~ /name="fusion.complete"/' $V/logs/vespa/vespa.log | grep -c webpage || true)
    [ "$s" -le "$c" ] && break; sleep 10
  done
  date +%s.%N > $D/settle.end
  sudo cat /proc/$pid/io > $D/io.after
  kill $SAMPLER $IOSTAT ;;
check)
  NAME=$2; D=$OUT/$NAME
  W=($(~/harness/bin/r6load vocab 2 3 10 50 300 2000 20000 150000))
  for q in true "body contains \"${W[0]}\"" "body contains \"${W[1]}\"" "title contains \"${W[2]}\"" \
           "default contains \"${W[3]}\"" "body contains \"${W[4]}\"" "body contains \"${W[5]}\"" \
           "body contains \"${W[6]}\"" "body contains \"${W[7]}\"" "body contains \"${W[0]}\" and title contains \"${W[2]}\""; do
    c=$(curl -s --data-urlencode "yql=select * from webpage where $q" --data-urlencode hits=0 --data-urlencode ranking=unranked --data-urlencode timeout=60s -G http://localhost:8080/search/ | jq .root.fields.totalCount)
    echo "$c	$q"
  done | tee $D/hits.txt ;;
esac
