# Lab 3 — Data Hazards on the Pipelined RV32I Simulator (`lab3/`)

## What was built

Everything is in `pipeline/`, plus one `int forwarding` field in `Pipeline` and one flag
(`--pipeline-forward`) in `main.c`. Both policies stay selectable:
`--pipeline` is the Lab 2 stalling pipeline, `--pipeline-forward` adds forwarding, and
`--batch` is still the single-cycle reference.

| File | Content |
|---|---|
| `hazard.h` | `HazardVerdict {stall, load_use}`, `Operands {a, b, fwd_ex, fwd_mem}`, `produces()`, `hazard_check()`, `forward_operands()` |
| `hazard.c` | Task 1: the stalling detector (any of ID/EX, EX/MEM, MEM/WB produces a source). Task 2: one forwarding mux per operand, EX/MEM before MEM/WB. Task 3: load-use, ID/EX only, tested on `mem_read` |
| `pipeline.c` | ID calls `hazard_check()`; EX calls `forward_operands()` and uses the result in all four places: ALU operands, store data, branch comparison, `jalr` target |
| `pipeline_state.h` | `forwarding`, `load_use_stalls`, `fwd_ex`, `fwd_mem` |

`produces()` is the single "is this latch my producer?" test. It requires a valid latch,
`reg_write`, `rd ≠ x0` and the same register number. An unused source becomes x0
(`uses_rs1 ? rs1 : 0`), so `lui`/`jal` never stall on the junk in their rs fields.

A stall performs all three of its actions:
1. a bubble enters EX
2. IF/ID is frozen
3. IF does not fetch and the PC does not move

## Results

Stalling only (`--pipeline`):

| Program | Instructions | Cycles | Stalls | Flushes | CPI |
|---|---|---|---|---|---|
| micro.s | 5 | 15 | 6 | 0 | 3.000 |
| sample1_sum_loop.s | 53 | 108 | 40 | 11 | 2.038 |
| sample2_mem_and_branches.s | 13 | 26 | 8 | 1 | 2.000 |
| sample4_data_hazards.s | 29 | 69 | 35 | 1 | 2.379 |

Forwarding and load-use detection (`--pipeline-forward`):

| Program | Cycles | Stalls | Flushes | CPI | fwd EX/MEM | fwd MEM/WB | load-use | Cycles saved |
|---|---|---|---|---|---|---|---|---|
| micro.s | 9 | 0 | 0 | 1.800 | 2 | 1 | 0 | 40% |
| sample1_sum_loop.s | 68 | 0 | 11 | 1.283 | 13 | 11 | 0 | 37% |
| sample2_mem_and_branches.s | 18 | 0 | 1 | 1.385 | 2 | 2 | 0 | 31% |
| sample4_data_hazards.s | 36 | 2 | 1 | 1.241 | 9 | 7 | 2 | 48% |

Every figure matches the sheet's tables, and so does the summary line. In both modes, `retired`
equals the single-cycle instruction count and the register file is identical to `--batch`.
sample4 prints 36 and ends with every register in the appendix's list (`gp = 0x78`, `s9 = 36` —
the squashed `addi s9, zero, 0` never ran). The `micro.s` trace reproduces the two diagrams of
Section 4: stalls in cycles 3–5 and 7–9 and 15 cycles without forwarding; a clean diagonal and
9 cycles with it.

Identity check, forwarding mode, sample4: 29 + 2 + 1 + 4 = 36.

## Explaining the numbers

- **Distance decides the cost.** Without forwarding a distance-1 dependence costs 3 stalls,
  distance 2 costs 2, and distance 3 costs 1. WB writes the register file in the same cycle ID
  reads it, but the Lab 2 detector still waits while the producer sits in MEM/WB. `micro.s` has
  two distance-1 pairs: 6 stalls.
- **Forwarding removes every stall except load-use.** A distance-1 value comes from EX/MEM, a
  distance-2 value from MEM/WB, and distance 3 needs no path at all. sample1 drops from 40
  stalls to 0. What remains are its 11 flushes (`j loop` ten times, `beq` once).
- **Load-use is the one unavoidable stall.** A load's value exists at the end of MEM, but the
  next instruction needs it at the start of EX, the same cycle. One stall puts the consumer in
  EX while the load is in WB, and the existing MEM/WB path delivers the value. sample4 has two
  (`lw s4 → addi s5`, `lw s6 → add s7`). Our extra test `tests/load_use4.s` covers all four
  shapes the sheet lists: ALU operand, branch operand, store data, address base.
- **EX/MEM must win over MEM/WB** because it holds the younger write. `tests/priority.s` prints 2;
  swapping the priority prints 1.

**The MEM→MEM path (store data after a load).** `lw t0; sw t0, …` costs one load-use stall here.
A dedicated MEM→MEM bypass could forward the loaded word straight into the store's MEM stage,
since the store needs its data only in MEM. That saves exactly that one cycle per load→store
pair (in `load_use4.s`, 1 of its 5 load-use stalls) and adds a mux and a comparator in MEM.

**WAR and WAW cannot occur.** Every instruction reads registers in ID and writes in WB, in
program order. A younger instruction can never write a register before an older one has read
it, or write it before an older one does.

## One design choice, stated

Stalls and flushes are counted when they happen, exactly as the sheet's code does
(`nx.stalls++` with the stall). A stall caused by an instruction fetched after the final exit
`ecall` is therefore counted even though that instruction never retires. For such programs,
cycles can come out slightly below `retired + stalls + flushes + 4`, never above. None of the
sheet's programs have code after the exit, and every figure above satisfies the identity exactly.
