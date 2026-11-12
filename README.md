# RV32 Pipeline Simulator — Computer Architecture Labs

A RISC-V simulator in C11, grown lab by lab from the single-cycle RV32I handout
(`riscv_sim_base`) into a five-stage pipeline with hazard detection, forwarding,
branch prediction, and the M, Zicsr and F (single-precision floating point)
extensions.

Each lab lives in its own folder, a complete copy of the simulator as it stood
at the end of that lab, because the lab sheets define different command-line
behaviour (in Lab 2 `--batch` is the single-cycle machine; from Lab 3 on it is
the pipeline and `--single-cycle` is the reference).

| Folder | Lab | Adds |
|---|---|---|
| `lab2/` | Five-stage pipeline | IF/ID/EX/MEM/WB latches, control signals, precise exceptions, naive 3-latch stalling, branches resolved in EX (1-cycle flush) |
| `lab3/` | Hazard unit + forwarding | pluggable `--hazard forward\|stall`, EX/MEM and MEM/WB forwarding, load-use stall, 2-cycle redirects, `--trace` |
| `lab4/` | Branch prediction | IF pre-decode (`jal` free), pluggable `--predictor not-taken\|btfn\|1bit\|2bit` |
| `lab5/` | Floating point | f registers and `fcsr`, RV32F (26 instructions, 5 rounding modes, IEEE flags), Zicsr, RV32M, FP-aware assembler |

## Build

Needs a C11 compiler and `make` (and Python 3 for the test generators).

```sh
make            # builds lab2/rvsim .. lab5/rvsim
make test       # runs every check below (about 30 s)
```

## Run

```sh
# Lab 2
./lab2/rvsim prog.s --batch                 # single-cycle reference
./lab2/rvsim prog.s --pipeline [--trace]    # five-stage pipeline

# Labs 3-5
./lab5/rvsim prog.s --batch                 # pipeline: forwarding + 2-bit predictor
./lab5/rvsim prog.s --batch --single-cycle  # reference
./lab5/rvsim prog.s --batch --hazard stall --predictor btfn --trace
./lab5/rvsim prog.s                         # interactive debugger (step, regs, mem, break, run)
```

Options common to all labs: `--max-cycles N`, `--mem-size N`.

Syscalls: `a7 = 1` prints the integer in `a0`, `a7 = 10` exits. Registers start
at zero (including `sp`); set up a stack yourself if you need one.

The assembler accepts the base RV32I mnemonics plus `li la mv j ret nop`,
`beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu neg not seqz snez sltz sgtz`,
`jr call tail`, one- and three-operand `jalr`, directives `.text .data .globl
.word .half .byte .align .space`, and in Lab 5 the M/F/Zicsr instructions,
`.float`, an optional rounding-mode operand (`rne rtz rdn rup rmm dyn`) and the
pseudo-ops `fmv.s fneg.s fabs.s frflags fsflags frrm fsrm frcsr fscsr csrr csrw`
and friends. Register names are case-insensitive.

## How it is verified

| Check | What it proves |
|---|---|
| `golden.sh` | Every figure published in the lab sheets (66 checks: cycles, stalls, flushes, forwards, mispredicts per program and predictor) |
| `labN/compare.sh` (`make test` in a lab) | Every program, under every hazard unit × predictor, leaves the same x/f registers, fcsr and output as the single-cycle reference, and `cycles = retired + 4 + 2·redirects + stalls` holds |
| `tests/fuzz.py <lab> <n> [seed] [--fp]` | Random, always-terminating programs (dependence chains, load-use, all load/store widths, loops, calls, indirect jumps; with `--fp` all of M and F on special values and every rounding mode) through `compare.sh` |
| `tests/fpref.py lab5 <n> [seed]` | RV32F results and fflags against an independent exact-rational IEEE model (ties, FLT_MIN boundary, signed-zero cancellation, NaNs) |
| `tests/intref.py <lab> <n> [seed] [--m]` | RV32I/M semantics against Python arithmetic |
| `tests/demo/run.sh` | Classic programs (Fibonacci, recursive factorial, bubble sort, GCD, Collatz, binary search, string copy, matrix multiply, π in float32) print the right answer in every lab and mode |

Each generator was checked by planting known bugs (missing MEM/WB forwarding,
missing FMA third-operand dependence, broken RMM tie-rounding, host-dependent
underflow, wrong `sra`) and confirming it reports them.

CI (`.github/workflows/ci.yml`) builds every lab with `-Werror` and runs
`make test` on Ubuntu (gcc, x86-64) and macOS (clang, arm64).

## Reports and notes

- `reports/lab4_report.md`, `reports/lab5_report.md` (+ raw `--batch` outputs)
- `reports/lab2_report.md`, `reports/lab3_report.md`
- `PLAN.md` — design decisions, deviations from the lab sheets and why
