#!/bin/sh
# Runs every demo program in every lab, in every mode that lab has, and
# checks the printed output against tests/demo/expected.txt. Machine state
# (registers, fcsr) is compared with the oracle by each lab's compare.sh.
cd "$(dirname "$0")/../.."
pass=0; fail=0
grep -v '^#' tests/demo/expected.txt | while read prog want labs; do
  for lab in lab2 lab3 lab4 lab5; do
    [ "$labs" = all ] || [ "$labs" = "$lab" ] || continue
    case $lab in
      lab2) modes="--batch|--pipeline" ;;
      lab3) modes="--batch|--pipeline|--pipeline-forward" ;;
      *)    modes="--single-cycle"
            for h in forward stall; do for p in not-taken btfn 1bit 2bit; do
              modes="$modes|--batch --hazard $h --predictor $p"; done; done ;;
    esac
    echo "$modes" | tr '|' '\n' | while read m; do
      got=$(./$lab/rvsim tests/demo/$prog $m 2>/dev/null | sed -n 2p)
      if [ "$got" = "$want" ]; then echo "PASS"; else echo "FAIL $lab $prog [$m]: printed '$got', want '$want'"; fi
    done
    (cd $lab && ./compare.sh ../tests/demo/$prog) | grep -v " MATCH  " | grep . | sed "s/^/FAIL $lab compare: /"
  done
done | awk '/^PASS/{p++; next} {print; f++} END{printf "demo: %d output checks passed, %d failures\n", p, f; exit f>0}'
