#!/bin/sh
# Diff --pipeline (stalling) and --pipeline-forward against the --batch
# single-cycle oracle: registers, program output, final status. Stalls and
# flushes are counted when they happen, so one behind the final ecall never
# reaches WB: cycles <= retired + stalls + flushes + 4, never above.
[ $# -eq 0 ] && set -- tests/*.s
fail=0
for f in "$@"; do
  ref=$(./rvsim "$f" --batch 2>&1)
  r=$(echo "$ref" | grep -E '^x|^status' | sed 's/cycles.*//'); ro=$(echo "$ref" | sed -n '2p')
  for m in --pipeline --pipeline-forward; do
    got=$(./rvsim "$f" $m 2>&1)
    g=$(echo "$got" | grep -E '^x|^status' | sed 's/cycles.*//'); go=$(echo "$got" | sed -n '2p')
    line=$(echo "$got" | grep '^pipeline: mode')
    kv() { echo "$line" | tr ' ' '\n' | sed -n "s/^$1=//p"; }
    st="MATCH"; [ "$r" = "$g" ] && [ "$ro" = "$go" ] || st="MISMATCH"
    [ "$(kv cycles)" -le $(( $(kv retired) + $(kv stalls) + $(kv flushes) + 4 )) ] || st="$st IDENTITY-FAIL"
    [ "$st" = "MATCH" ] || fail=1
    printf '%-28s %-18s %s  %s\n' "$(basename "$f")" "$m" "$st" "$(echo "$line" | cut -d' ' -f2-)"
  done
done
exit $fail
