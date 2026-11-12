# Lab 3 — Hazard Detection and Forwarding (`lab3/`)

No Lab 3 sheet was available; the behaviour below is what the Lab 4 and Lab 5 sheets state the
Lab 3 pipeline does, and it reproduces the "previous lab" column of the Lab 4 sheet exactly.

## What was built

- **A pluggable hazard unit** (`pipeline/hazard.h/.c`), chosen with `--hazard forward|stall`
  (default `forward`). Each unit is two functions: `stall()` asked in ID, and `forward()` asked
  in EX for each source register (`FWD_NONE / FWD_EX_MEM / FWD_MEM_WB`).
  - `forward`: EX/MEM → EX and MEM/WB → EX forwarding (the younger producer wins); the only stall
    is load-use — a load in EX whose `rd` the instruction in ID reads (as an ALU source, a
    branch operand or store data): one bubble, then MEM/WB forwarding.
  - `stall`: interlock, no forwarding — hold ID while ID/EX or EX/MEM will write a source. The
    register file is written in WB before ID reads it in the same cycle, so MEM/WB never needs a
    stall (Lab 2's naive unit stalled for it too).
- **Branches and jumps** resolve in EX using forwarded operands. Fetch always continues at pc+4;
  a taken branch or any jump squashes the two younger instructions (IF and ID) and fetch restarts
  at the target the next cycle: two cycles. Identity:
  `cycles = retired + 4 + 2 × redirects + stalls`, with redirects = taken branches + `jal` + `jalr`.
- **`--single-cycle`** runs the reference; `--batch` now means the pipeline. `--trace` prints the
  instruction in every stage each cycle, in the format the Lab 4 sheet shows.

## Results (retired / cycles / stalls / forwards / redirects / CPI)

| Program | forward | stall (interlock) |
|---|---|---|
| sample1_sum_loop | 53 / 79 / 0 / 24 / 11 / 1.491 | 53 / 105 / 26 / – / 11 / 1.981 |
| sample2_mem_and_branches | 13 / 19 / 0 / 4 / 1 / 1.462 | 13 / 24 / 5 / – / 1 / 1.846 |
| sample3_hazards | 18 / 29 / 1 / 7 / 3 / 1.611 | 18 / 40 / 12 / – / 3 / 2.222 |
| t1_counted_loop | 37 / 59 / 0 / 12 / 9 / 1.595 | 37 / 81 / 22 / – / 9 / 2.189 |
| t2_nested_loops | 52 / 78 / 0 / 25 / 11 / 1.500 | 52 / 118 / 40 / – / 11 / 2.269 |
| t3_alternating | 50 / 84 / 0 / 16 / 15 / 1.680 | 50 / 116 / 32 / – / 15 / 2.320 |
| t4_calls | 219 / 344 / 15 / 46 / 53 / 1.571 | 219 / 410 / 81 / – / 53 / 1.872 |

The forward column for t1–t4 (59 / 78 / 84 / 344) is the Lab 4 sheet's "Lab 2" column, and t1's
12 forwards match its statistics block. t1–t4 were rebuilt from the Lab 4 sheet's descriptions.

Forwarding removes every stall except load-use: sample1 goes from 105 to 79 cycles (−25%), and the
only stalls left anywhere are load-use bubbles (sample3: one; t4: 15 — nine table loads and six
parameter loads, each used by the very next instruction).
What remains above CPI 1 is control: two cycles per taken branch or jump, which Lab 4's
predictors and IF pre-decode attack.
