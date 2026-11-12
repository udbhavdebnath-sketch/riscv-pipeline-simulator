#!/bin/sh
# Diff pipeline vs single-cycle oracle: registers, program output, status; check cycle identity.
for f in "${@:-tests/*.s}"; do
  ref=$(./rvsim "$f" --batch 2>&1); got=$(./rvsim "$f" --pipeline 2>&1)
  r=$(echo "$ref" | grep -E '^x|^status' | sed 's/cycles.*//'); g=$(echo "$got" | grep -E '^x|^status' | sed 's/cycles.*//')
  ro=$(echo "$ref" | sed -n '2p'); go=$(echo "$got" | sed -n '2p')
  v() { echo "$got" | awk -v k="$1" '$1==k{print $3}'; }
  id=$(( $(v retired) + $(v stalls) + $(v flushes) + 4 ))
  st="MATCH"; [ "$r" = "$g" ] && [ "$ro" = "$go" ] || st="MISMATCH"
  # Stalls and flushes are counted when they happen (as in the lab sheets), so one that occurs
  # behind the final ecall never reaches WB: cycles can be below the identity, never above.
  [ "$(v cycles)" -le "$id" ] || st="$st IDENTITY-FAIL($id vs $(v cycles))"
  printf '%-40s %s  single=%s pipe=%s stalls=%s flushes=%s CPI=%s\n' "$f" "$st" \
    "$(echo "$ref" | sed -n 's/.*cycles = //p')" "$(v cycles)" "$(v stalls)" "$(v flushes)" "$(v CPI)"
done
