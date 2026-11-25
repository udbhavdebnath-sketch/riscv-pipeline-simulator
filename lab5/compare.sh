#!/bin/sh
# Diff the pipeline (every hazard unit x every predictor) against the
# --single-cycle oracle: registers, program output, final status. Check
#   cycles    = retired + 4 + 2 x redirects + stalls
#   redirects = mispredicts + jalr   (<=, see below)
# on every run. Prints one line per program/hazard/predictor.
fail=0
[ $# -eq 0 ] && set -- tests/*.s
for f in "$@"; do
  ref=$(./rvsim "$f" --batch --single-cycle 2>&1)
  r=$(echo "$ref" | grep -E '^(x[0-9]|f[0-9]|fcsr|pc |status)' | sed 's/cycles.*//'); ro=$(echo "$ref" | sed -n '2p')
  for h in forward stall; do
    for p in not-taken btfn 1bit 2bit; do
      got=$(./rvsim "$f" --batch --hazard $h --predictor $p 2>&1)
      g=$(echo "$got" | grep -E '^(x[0-9]|f[0-9]|fcsr|pc |status)' | sed 's/cycles.*//'); go=$(echo "$got" | sed -n '2p')
      v() { echo "$got" | awk -v k="$1" '$1==k{print $3}'; }
      cyc=$(echo "$got" | sed -n 's/.*cycles = //p')
      st="MATCH"; [ "$r" = "$g" ] && [ "$ro" = "$go" ] || st="MISMATCH"
      [ $(( $(v retired) + 4 + 2 * $(v redirects) + $(v stalls) )) = "$cyc" ] || st="$st CYCLE-IDENTITY-FAIL"
      # <= rather than =: a jalr whose target happens to be pc+4 was "guessed"
      # right by IF and is not redirected (tests/e_control.s has one).
      [ "$(v redirects)" -le $(( $(v mispredicts) + $(v jalr) )) ] || st="$st REDIRECT-IDENTITY-FAIL"
      [ "$st" = "MATCH" ] || fail=1
      printf '%-26s %-7s %-9s %s  cycles=%-4s stalls=%-3s fwd=%-3s br=%-3s mis=%-3s jal=%-3s jalr=%s\n' \
        "$(basename $f)" $h $p "$st" "$cyc" "$(v stalls)" "$(v forwards)" "$(v branches)" "$(v mispredicts)" "$(v jal)" "$(v jalr)"
    done
  done
done
exit $fail
