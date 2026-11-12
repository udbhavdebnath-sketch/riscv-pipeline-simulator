#!/bin/sh
# Checks every figure the lab sheets publish (and that our reconstructed test
# programs reproduce). Guards the timing *policy*; tests/fuzz.py and each
# lab's compare.sh guard correctness. Prints only failures, then a summary.
cd "$(dirname "$0")"
pass=0; fail=0
# check <lab> <program> "<flags>" <stat> <expected>
check() {
  out=$("./$1/rvsim" "$1/tests/$2" $3 2>/dev/null)
  case $4 in
    cycles) got=$(echo "$out" | sed -n 's/.*cycles = //p') ;;
    output) got=$(echo "$out" | sed -n 2p) ;;
    *)      got=$(echo "$out" | awk -v k="$4" '$1==k{print $3}') ;;
  esac
  if [ "$got" = "$5" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $1 $2 $3: $4 = $got, sheet says $5"; fi
}
for d in lab2 lab3 lab4 lab5; do make -C $d -s >/dev/null || exit 1; done

# Lab 2 (W7 sheet section 7): naive 3-latch stalling, branches resolved in EX
for row in "sample1_sum_loop.s 108 40 11" "sample2_mem_and_branches.s 26 8 1" "sample3_hazards.s 44 19 3"; do
  set -- $row
  check lab2 $1 --pipeline cycles $2; check lab2 $1 --pipeline stalls $3; check lab2 $1 --pipeline flushes $4
done

# Lab 3: the "Lab 2" column of the Lab 4 sheet (forwarding, fetch pc+4, 2-cycle redirects)
for row in "t1_counted_loop.s 59" "t2_nested_loops.s 78" "t3_alternating.s 84" "t4_calls.s 344"; do
  set -- $row; check lab3 $1 --batch cycles $2
done
check lab3 t1_counted_loop.s --batch forwards 12

# Lab 4 sheet section 9: cycles and mispredicts, every program x predictor
for row in "t1_counted_loop.s 59 43 45 45 9 1 2 2" "t2_nested_loops.s 78 66 76 70 11 5 10 7" \
           "t3_alternating.s 76 64 74 74 11 5 10 10" "t4_calls.s 278 278 280 278 2 2 3 2"; do
  set -- $row; f=$1; shift
  for p in not-taken btfn 1bit 2bit; do check lab4 $f "--batch --predictor $p" cycles $1; shift; done
  for p in not-taken btfn 1bit 2bit; do check lab4 $f "--batch --predictor $p" mispredicts $1; shift; done
done
check lab4 t4_calls.s --batch jal 33
check lab4 t4_calls.s --batch jalr 18
check lab4 t4_calls.s --batch redirects 20

# Lab 5 sheet section 9 (forwarding columns; t1 also the interlock column)
for row in "t1_fp_arith.s 36 41 1" "t2_conversions.s 109 113 0" "t3_special_values.s 72 76 0" "t4_fp_program.s 133 159 14"; do
  set -- $row; check lab5 $1 --batch retired $2; check lab5 $1 --batch cycles $3; check lab5 $1 --batch stalls $4
done
check lab5 t1_fp_arith.s --batch forwards 11
check lab5 t1_fp_arith.s "--batch --hazard stall" cycles 56
check lab5 t1_fp_arith.s "--batch --hazard stall" stalls 16
check lab5 t4_fp_program.s --batch redirects 4
check lab5 t4_fp_program.s --batch output 12000014141000

echo "golden: $pass passed, $fail failed"
[ $fail -eq 0 ]
