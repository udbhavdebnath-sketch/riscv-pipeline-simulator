# Final RV32IMF Pipeline Simulator

Student IDs: 2024A7PS0121H and 2024A7PS0107H.

A C11 RISC-V simulator with RV32I, M, F and Zicsr instructions, a five-stage
pipeline, hazard handling, branch prediction, and floating-point fcsr support.

## Build and run

Requires Linux or WSL, GCC and GNU Make. No additional libraries are needed
beyond the standard C library and libm.

```sh
make
./rvsim tests/t1_fp_arith.s --batch
./rvsim tests/t1_fp_arith.s --batch --hazard stall --predictor btfn
./rvsim tests/t1_fp_arith.s --batch --single-cycle
make test
```

Options: `--batch`, `--single-cycle`, `--hazard none|stall|forward`,
`--predictor not-taken|btfn|1bit|2bit`, `--trace`, `--max-cycles N`, and
`--mem-size N`. Defaults are forwarding and a two-bit predictor. Without
`--batch`, the simulator opens its interactive debugger.

## Layout

- `core/`: register files, memory, decoder, executor and SimEngine interface.
- `isa/`: RV32I/M/F and CSR handlers, including floating-point helpers.
- `assembler/`: assembler and instruction encoding.
- `pipeline/`: pipeline stages, controls, hazard handling and predictors.
- `ui/`: the supplied final reference UI.
- `tests/` and `compare.sh`: local regression programs and comparisons.
- `main.c` and `Makefile`: entry point and standalone build.

The SimEngine interface and reference UI come from the supplied updated
course package. The evaluator can replace `ui/` and build from source.

## Evaluation

Run from the updated student package directory:

```sh
python3 evaluate.py /path/to/this/folder sample.s sample.txt --verbose
```

The official sample matches all 65 graded register values and all nine
counters. All 15 supplied branch cases and eight floating-point cases also
match. These results do not establish hidden-test marks or MOSS similarity.

Pipeline ECALL operands use the same interlocks and forwarding as other EX
operands. Exit and faults are reported at WB with the stopping instruction's
PC. Direct JAL is predicted in IF; older pre-prediction lab cycle counts use
a different timing policy.
