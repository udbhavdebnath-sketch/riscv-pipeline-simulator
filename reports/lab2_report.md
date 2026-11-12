# Lab 2 — Building a Five-Stage Pipeline (`lab2/`)

## What was built

All new code is in `pipeline/` (`control.h/.c`, `pipeline_state.h`, `pipeline.c`); `main.c`
gains `--pipeline` (and `--trace`), the Makefile two lines.

- **Control signals** are generated once in ID by `control_decode()`, a single switch on the
  registry's instruction format, and travel with the instruction. Later stages act only on them.
- **Latches** IF/ID, ID/EX, EX/MEM, MEM/WB are C structs with `valid` and a `fault` code.
  `pipeline_cycle()` reads the old latch set `p` and writes the new one `nx`, then commits
  `*p = nx` — one clock edge, so the order the stages are written in cannot matter.
- **Precise exceptions.** A fault (illegal instruction, fetch or memory out of bounds) is
  recorded in the latch and only becomes a `CPUStatus` in WB, so instructions fetched past the
  final `ecall` (the `.data` words) never stop the program before it exits.
- **Hazards (Task 4, reconstructed — the sheet's pages for Tasks 4–6 were blank).** Naive
  3-latch stalling: ID holds an instruction while any valid, register-writing instruction in
  ID/EX, EX/MEM or MEM/WB has an `rd` it reads; IF/ID and the fetch PC freeze, a bubble enters EX.
- **Branches and jumps (Task 5)** resolve in EX; when taken, the instruction in ID is killed and
  IF fetches the target in the same cycle, so the penalty is one cycle. This is the only policy
  under which the sheet's identity holds with 11 flushes for sample1.
- **Counters.** Like the lab sheets' own code, a stall is counted in the cycle ID is held and
  a flush in the cycle a taken branch or jump squashes ID. For every program that ends with
  its exit `ecall` (all of the sheet's), `cycles = retired + stalls + flushes + 4` holds exactly.
  A stall or flush caused by code fetched after the exit is counted but never reaches WB, so
  there cycles can fall slightly below the identity, never above. The cycle in which a fault
  reaches WB is not counted, so the 5-`addi` example (which simply runs off its end) gives 9.

## Results

| Program | single-cycle | pipeline | retired | stalls | flushes | CPI |
|---|---|---|---|---|---|---|
| sample1_sum_loop | 53 | 108 | 53 | 40 | 11 | 2.038 |
| sample2_mem_and_branches | 13 | 26 | 13 | 8 | 1 | 2.000 |
| sample3_hazards | 18 | 44 | 18 | 19 | 3 | 2.444 |

All three rows match the sheet; the registers and printed output match the single-cycle run.
`sample3_hazards.s` was not in the handout (Appendix A was missing); ours was written to the
sheet's description (data hazards at distance 1–3, a store-data hazard, a load-use pair, a taken
branch, a call and a return) and reproduces its row exactly. Its called function sits before
`main`, so no code follows the exit `ecall`.
Identity check, sample1: 53 + 40 + 11 + 4 = 108.

## Single-cycle vs pipelined

The appendix example (five independent `addi`): 9 pipeline cycles, CPI 1.80, against 25 for a
machine that spends five cycles per instruction — a 64% reduction.

Our single-cycle simulator counts one (long) cycle per instruction, so its raw count is lower
than the pipeline's. Measured in the same unit — one pipeline stage, a single-cycle instruction
taking five of them — the comparison is:

| Program | single-cycle (×5 stages) | pipeline | change | pipeline CPI |
|---|---|---|---|---|
| sample1_sum_loop | 265 | 108 | −59% | 2.038 |
| sample2_mem_and_branches | 65 | 26 | −60% | 2.000 |
| sample3_hazards | 90 | 44 | −51% | 2.444 |
| 5 × `addi` | 25 | 9 | −64% | 1.800 |

The gap between the ideal (CPI → 1) and the measured CPI is entirely stalls and flushes. In
sample1, `beq t0, t2` reads `t0` one instruction after `addi t0` writes it, so with no forwarding
it waits three cycles on every one of the 11 passes (33 stalls); the loop entry costs 1 more and
the tail's `li gp` → `sw a0, 0(gp)` chain 6 more (40). `j loop` runs ten times and `beq` is taken
once: 11 one-cycle flushes. Lab 3's forwarding unit removes all 40 stalls.
