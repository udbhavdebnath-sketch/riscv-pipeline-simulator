#!/bin/sh
# Diff the pipeline (every hazard unit) against the --single-cycle oracle:
# registers, program output and final status; check
# cycles = retired + 4 + 2 x redirects + stalls on every run.
[ $# -eq 0 ] && set -- tests/*.s
for f in "$@"; do
  ref=$(./rvsim "$f" --batch --single-cycle 2>&1)
  r=$(echo "$ref" | grep -E '^x|^status' | sed 's/cycles.*//'); ro=$(echo "$ref" | sed -n '2p')
  for h in forward stall; do
    got=$(./rvsim "$f" --batch --hazard $h 2>&1)
    g=$(echo "$got" | grep -E '^x|^status' | sed 's/cycles.*//'); go=$(echo "$got" | sed -n '2p')
    v() { echo "$got" | awk -v k="$1" '$1==k{print $3}'; }
    cyc=$(echo "$got" | sed -n 's/.*cycles = //p')
    st="MATCH"; [ "$r" = "$g" ] && [ "$ro" = "$go" ] || st="MISMATCH"
    [ $(( $(v retired) + 4 + 2 * $(v redirects) + $(v stalls) )) = "$cyc" ] || st="$st IDENTITY-FAIL"
    printf '%-30s %-7s %s  cycles=%-4s retired=%-4s stalls=%-3s fwd=%-3s br=%-3s jal=%-3s jalr=%-3s redir=%s\n' \
      "$(basename $f)" $h "$st" "$cyc" "$(v retired)" "$(v stalls)" "$(v forwards)" "$(v branches)" "$(v jal)" "$(v jalr)" "$(v redirects)"
  done
done
