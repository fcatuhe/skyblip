#!/usr/bin/env bash
# usage: scripts/bench/fetch_both.sh <tag>   e.g. tag=0800 (HHMM of the run start; reuse it to resume)
# scan, diag + timing (15 tries), fetch (30 tries) for both units, into ~/skyblip-captures/<name>-<tag>.ndjson
set -u
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
B="mise x uv@latest -- uv run -q --script --with bleak scripts/blip.py"
tag=${1:?tag}
out=${SKYBLIP_CAPTURES:-$HOME/skyblip-captures}; mkdir -p "$out"
stamp=$(date +%H%M)
$B scan > $out/scan-$tag-$stamp.log 2>&1; cat $out/scan-$tag-$stamp.log
for pair in "0B1B2C D7:F4:67:4D:6C:B2" "E68BD9 ED:9A:32:0D:9E:54"; do
  set -- $pair; name=$1; addr=$2
  for cmd in diag timing; do
    f=$out/$name-$tag-$cmd-$stamp.json
    for i in $(seq 1 15); do
      $B --address $addr cmd "{\"cmd\":\"$cmd\"}" > $f 2>$f.err && { echo "$name $cmd ok try $i"; break; }
      echo "$name $cmd try $i rc $?"; sleep 2
    done
  done
  for i in $(seq 1 30); do
    $B --address $addr fetch --log diagnostics --out $out/$name-$tag.ndjson > $out/$name-$tag-fetch.log 2>&1
    rc=$?
    [ $rc -eq 0 ] && { echo "$name fetch ok try $i"; break; }
    echo "$name fetch try $i rc $rc: $(tail -1 $out/$name-$tag-fetch.log)"; sleep 3
  done
  python3 scripts/blip.py decode $out/$name-$tag.ndjson --summary 2>&1 | tail -25
done
