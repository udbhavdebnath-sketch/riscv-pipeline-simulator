# Final submission validation

Reviewed on 25 November 2026 for student IDs 2024A7PS0121H and 2024A7PS0107H.
The final simulator is `lab5/`; `lab2/`, `lab3/` and `lab4/` retain historical
implementations. The repository root builds every lab and is not the evaluator
submission root.

## Submission

The archive in `submission/Final_2024A7PS0121H_2024A7PS0107H.zip` contains one
folder, `FA_2024A7PS0121H_2024A7PS0107H`, with its own Makefile and 51 source,
documentation and test files. It excludes compiled binaries, object files,
Git metadata, earlier lab versions, and the temporary timing audit build.
All archive files match `lab5/` after line-ending normalization.

SHA-256:
`32b363939c073c010a10f1d52afc45b3cce6b6055692dff862bfb5443b57a2c8`

The announcement specifies `Final_<IDs>.zip`; the steps PDF uses `FA_<IDs>.zip`.
The archive follows the announcement's filename and the PDF's inner-folder
layout. The stated deadline is 25 November 2026, 23:59 IST. Creating or publishing
the archive on GitHub does not submit it to the course portal.

## Materials reviewed

| Supplied material | Check and finding |
|---|---|
| GitHub repository | Final implementation, historical lab versions, reports, CI and regression tests reviewed. |
| `riscv_sim_base_updated.zip` | Compared interfaces and TODO descriptions; adopted the supplied SimEngine interface and single-cycle wrapper. Existing simulator logic retained. |
| `student_package_updated.zip` | Read README and evaluator; tested replacement reference UI, clean build, command-line options, FP register/fcsr accessors and counter parsing. |
| `Steps to follow.pdf` | Checked source layout and evaluator procedure; resolved filename difference as above. |
| `sample_tests.zip` | Read README, ran all 41 supplied expected-output cases and the three skeleton programs. Deliberate incorrect no-hazard results were preserved. |
| `Cycles references.docx` | Checked oracle and pipeline columns against sample outputs. The branch column uses 2bit, not the minimum over every predictor. |
| `W7_RISCV_Pipeline_Lab_Sheet.pdf` | Compared earlier pipeline timing with the historical implementation and new package. |
| `Simulator_LabSheet3_Forwarding_HDU.pdf` | Compared interlocks, forwarding and old flush-count conventions. |
| `Lab Sheet 4 Branch Prediction on the Pipelined RV32I Simulator.pdf` | Checked IF prediction of direct JAL, branch predictor modes and redirect timing. |
| `Lab Sheet 5 Floating-Point Arithmetic (RV32F).pdf` | Checked FP/fcsr coverage and distinguished reconstructed local fixtures from supplied sample programs. |
| Announcement and rubric | Confirmed final RV32IMF/Zicsr scope, grading dimensions, student IDs and archive naming. Hidden-test and MOSS outcomes cannot be verified locally. |

## Measured results

- The official final evaluator, run on the exact ZIP: **100% registers and
  100% counters**, with all 65 graded values matching.
- After extracting the ZIP, replacing its UI and building from source:
  **24/24 final-stage cases pass**: the official sample, 15 branch cases and
  eight FP cases. Registers, PC, fcsr where supplied, status, printed output,
  graded counters and forwarding counts were compared.
- `make test` also passes in the evaluator-extracted archive. Its target invokes
  `sh ./compare.sh`, so ZIP extraction need not preserve executable bits.
- The full repository regression suite passes: 95 golden checks, 179 demo
  output checks, 500 independent integer cases for each of four labs, 2,000 FP
  reference cases and 320 randomized Lab 5 runs, plus other weekly regressions.
- Interface tests cover all 12 hazard/predictor combinations, single-cycle
  mode, tracing, cycle limits, ECALL operands and fault timing.
- The three skeleton programs match oracle cycles **41, 87, 634** and print
  **2016, 36, 755120**, respectively. Their RARS counts in the document are one
  lower because of the final exit instruction count.
- FP forward cycles are **46, 71, 222**; the two supplied stall cases are
  **61 and 449**. All match. Branch 2bit cycles are **1476, 4985, 3982**.
  For example, b1_patterns takes **1432** with btfn, so the document's branch
  column should not be interpreted as universally optimal.

## Earlier timing policies

The final implementation matches registers, PC, status and printed output for
all 41 supplied expected-output cases. **36/41 also match every compared
counter directly.** Five cases target the earlier design where JAL redirects
in EX rather than being predicted in IF:

| Case | Mode | Earlier reference cycles | Final cycles |
|---|---|---:|---:|
| p1_broad | no hazards | 113 | 111 |
| p2_edge | no hazards | 57 | 51 |
| p3_kernel | no hazards | 762 | 748 |
| h3_kernel | forward | 461 | 447 |
| h3_kernel | stall | 749 | 742 |

A temporary audit copy with only direct-JAL prediction disabled for the early
pipeline/hazard cases matches **41/41**, including counters. This supports the
timing-policy explanation; it is not the submitted implementation. Removing
JAL prediction from the final simulator would contradict the branch lab.

The historical folders are not all compatible with the new package: Lab 2/3
cycle counts differ, historical pipeline PCs report the speculative fetch PC,
and Lab 3 has no `none` CLI mode. Historical Lab 4 matches supplied branch
cycles but its final pipeline PC is 16 bytes too high. Use `lab5/` for final
evaluation, not these earlier versions.

## Changes for the final evaluator

- Added the reference SimEngine contract and UI integration.
- Printed counters as plain `name = value` lines.
- Reported exit/fault PC rather than speculative fetch PC.
- Added `--hazard none`.
- Routed ECALL a7/a0 operands through EX interlocks and forwarding, with exit
  committed at WB; stopped younger work after exit/fault.
- Counted the clock that delivers a WB fault and suppressed EX behind a MEM fault.
- Added focused regression checks and shell line-ending rules.

The repository's reconstructed t1 FP fixture places a0/a7 producers immediately
before ECALL. Three old `golden.sh` assertions were explicitly updated for the
final operand policy: forwards **11 -> 13**, stall cycles **56 -> 58**, stalls
**16 -> 18**. Independent ECALL tests verify the two interlock cycles. Supplied
course expected-output files were not changed. Earlier report tables remain
historical measurements, not current results.

## Reproduction and limits

From the repository root on Linux/WSL:

```sh
make
make test
```

From the unmodified updated student package directory:

```sh
python3 evaluate.py /path/to/Final_2024A7PS0121H_2024A7PS0107H.zip sample.s sample.txt --verbose
```

The supplied course packages are external inputs and are not required inside
the submission archive. The archive builds without files outside its root.

No MOSS check has been performed: the instructor's comparison corpus, base-code
exclusions and scoring configuration are unavailable. The five hidden tests
are also unavailable. These checks support submission readiness, not a
guarantee of 20/20. No course-portal submission has been performed.
