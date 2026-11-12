# Lab 4 — Branch Prediction on the Pipelined RV32I Simulator

All figures below were measured with our simulator (`lab4/`); the raw `--batch` outputs are in
`reports/lab4_outputs/`. Every run leaves the register file identical to `--single-cycle`.

## 1. What was built

| File | Content |
|---|---|
| `pipeline/branch_predictor.h/.c` | The interface as given in the sheet (`BPState`, `bp_index`, `BranchPredictor`) plus the predictor table and `bp_find()` |
| `pipeline/bp_static.c` | `not-taken`, `btfn` |
| `pipeline/bp_dynamic.c` | `1bit` (last outcome), `2bit` (saturating counter, reset to 1) |
| `pipeline/pipeline.c` | IF pre-decode: a conditional branch asks `predict(pc, target)`, a `jal` redirects IF itself, a `jalr` is not predicted (fetch continues at pc+4). The guess travels down the pipe as `pred_next`; EX calls `update(pc, target, taken)` and squashes IF+ID when the real next PC differs from it |

None of the predictors keep state outside `BPState`; `predict` takes `const BPState *` and only
`update` (in EX, where `taken` is a fact) writes the table.

## 2. Results

**Cycles** (Lab 3 = previous lab's pipeline: fetch always pc+4, every taken branch and jump costs 2):

| Program | Instrs | Lab 3 | not-taken | btfn | 1bit | 2bit | single-cycle |
|---|---|---|---|---|---|---|---|
| t1_counted_loop | 37 | 59 | 59 | 43 | 45 | 45 | 37 |
| t2_nested_loops | 52 | 78 | 78 | 66 | 76 | 70 | 52 |
| t3_alternating | 50 | 84 | 76 | 64 | 74 | 74 | 50 |
| t4_calls | 219 | 344 | 278 | 278 | 280 | 278 | 219 |
| **total** | 358 | 565 | 491 | 451 | 475 | 467 | 358 |
| **CPI** | | 1.578 | 1.372 | 1.260 | 1.327 | 1.305 | 1.000 |

**Mispredicts** (16 cells) and jumps:

| Program | Branches | not-taken | btfn | 1bit | 2bit | jal | jalr |
|---|---|---|---|---|---|---|---|
| t1_counted_loop | 10 | 9 | 1 | 2 | 2 | 0 | 0 |
| t2_nested_loops | 16 | 11 | 5 | 10 | 7 | 0 | 0 |
| t3_alternating | 16 | 11 | 5 | 10 | 10 | 4 | 0 |
| t4_calls | 20 | 2 | 2 | 3 | 2 | 33 | 18 |
| **total** | 62 | 33 | 13 | 25 | 21 | 37 | 18 |
| **accuracy** | | 46.8% | 79.0% | 59.7% | 66.1% | | |

Both tables match the sheet cell for cell. Both identities hold on every run:
`cycles = retired + 4 + 2 × redirects + stalls` and `redirects = mispredicts + jalr`
(e.g. t4/2bit: 219 + 4 + 2×20 + 15 = 278, and 2 + 18 = 20).

## 3. Per program and predictor, against the floor

The floor is the fewest misses any predictor that sees only the PC and target could make here:
each loop exit (no own-history predictor can foresee it) and, for t3, half of the alternating
branch's eight executions.

| Program | Pred. | Br | Miss | Acc. | Redir. | Cycles | CPI | Gap to floor |
|---|---|---|---|---|---|---|---|---|
| t1 (floor 1) | not-taken | 10 | 9 | 10.0% | 9 | 59 | 1.595 | +8: guesses N on all nine taken iterations of a backward loop. |
| | btfn | 10 | 1 | 90.0% | 1 | 43 | 1.162 | 0: knows from `target < pc` that the loop is taken; misses only the exit. |
| | 1bit | 10 | 2 | 80.0% | 2 | 45 | 1.216 | +1: the table starts at N, so the first execution is a warm-up miss. |
| | 2bit | 10 | 2 | 80.0% | 2 | 45 | 1.216 | +1: starts weakly-not-taken, same warm-up miss as 1bit. |
| t2 (floor 5) | not-taken | 16 | 11 | 31.2% | 11 | 78 | 1.500 | +6: every taken iteration of both loops is a miss. |
| | btfn | 16 | 5 | 68.8% | 5 | 66 | 1.269 | 0: misses only the four inner exits and the outer exit. |
| | 1bit | 16 | 10 | 37.5% | 10 | 76 | 1.462 | +5: each inner exit flips the bit, so each re-entry misses too, plus the outer warm-up. |
| | 2bit | 16 | 7 | 56.2% | 7 | 70 | 1.346 | +2: one warm-up miss per loop; hysteresis then absorbs every exit. |
| t3 (floor 5) | not-taken | 16 | 11 | 31.2% | 11 | 76 | 1.520 | +6: misses the seven taken loop branches and the four taken alternations. |
| | btfn | 16 | 5 | 68.8% | 5 | 64 | 1.280 | 0: loop exit plus the four taken forward alternations, the best any fixed guess can do. |
| | 1bit | 16 | 10 | 37.5% | 10 | 74 | 1.480 | +5: always guesses the previous outcome of T N T N, so all 8 alternations miss, plus the loop's warm-up. |
| | 2bit | 16 | 10 | 37.5% | 10 | 74 | 1.480 | +5: the counter swings between 1 and 2 and is wrong on all 8 alternations. |
| t4 (floor 2) | not-taken | 20 | 2 | 90.0% | 20 | 278 | 1.269 | 0: the two taken branches each happen once; 18 redirects are returns. |
| | btfn | 20 | 2 | 90.0% | 20 | 278 | 1.269 | 0: both branches are forward, so btfn guesses exactly like not-taken. |
| | 1bit | 20 | 3 | 85.0% | 21 | 280 | 1.279 | +1: after the one taken execution of the middle branch the bit says T, so the next execution misses. |
| | 2bit | 20 | 2 | 90.0% | 20 | 278 | 1.269 | 0: one T moves the counter 0 → 1, still predicting N. |

## 4. The three results the sheet asks about

**t4 drops from 344 to 278 before any prediction.** The not-taken predictor guesses exactly as the
Lab 3 pipeline did, yet t4 loses 66 cycles = 2 × 33 `jal`. That is the IF pre-decode: a `jal`'s
target is in its immediate, so IF redirects itself the same cycle and the jump costs nothing. The
18 returns (`jalr`) still cost 2 cycles each (36 cycles), because their target is in a register IF
cannot read and there is no return-address stack. That is why all four predictors score almost
the same on t4: only 2 of its 20 redirects are within a predictor's reach.

**btfn wins overall, with no state.** Loops branch backwards and are taken on every pass but the
last, so a single address comparison gets every loop right except its exit. The dynamic predictors
must first learn what btfn knows from the address arithmetic: one warm-up miss per branch. These
programs execute each branch only 10–20 times, too few to amortise that miss. "Dynamic" is not a
synonym for "better".

**2bit beats 1bit on t2 (7 vs 10) and ties it on t3 (10 vs 10).** On t2, hysteresis pays off. The
inner branch's counter saturates at 3, and the exit only knocks it down to 2, which still predicts
taken, so the re-entry is correct. The 1-bit predictor flips on the exit and misses the re-entry
as well: 2 misses per pass instead of 1. On t3, both fail on a pattern neither can represent. A
predictor keyed only on one branch's own recent outcomes cannot follow T N T N: the 1-bit one is
always one step behind, and the 2-bit counter just oscillates around its threshold.

## 5. Design decisions

**2-bit reset value = 1 (weakly not taken).** It costs one extra miss on the first execution of
every loop, but it never predicts taken for a branch it has never seen. Starting at 2 would remove
the first-execution miss on backward branches and add one on forward branches (t1 would drop to
1 miss; t3 would get worse). For these programs, as for most code, an unseen forward branch is
more likely to fall through, so we keep 1.

**Indexing and aliasing.** `bp_index(pc) = pc[7:2]` gives 64 entries. Branches whose addresses
differ by a multiple of 256 bytes share an entry and silently train each other: there is no tag,
so one loop's exit can make an unrelated branch mispredict. None of the four programs alias.
t1–t3 are under 70 bytes. t4 spans 296 bytes, but its three branches sit at 0x14, 0x5c and 0xbc,
which are table entries 5, 23 and 47. A real design would add a tag (or a larger table) so that
a collision is detected instead of silently mixing histories.

**What a predictor may see.** Only `pc` and `target`. `predict` is const, so a predictor cannot
learn from a guess, and all state lives in the pipeline-owned `BPState`, so the four predictors
coexist in one binary.

## 6. Beyond per-branch history: the next predictor

Fixing t3 needs correlating history. Keep a global history register (GHR) of the last *k*
conditional-branch outcomes and index the 2-bit counter table with `pc[7:2] XOR GHR` (gshare).
Each (branch, recent-pattern) pair then gets its own counter. For T N T N, the counter selected
after "…N" learns T, and the one selected after "…T" learns N. After warm-up the alternating
branch is predicted perfectly. This fits the given interface: the GHR lives in `BPState` (e.g. the
last table byte), `predict` reads it, and `update` shifts in `taken`. The catch is that `update`
runs in EX, so up to two younger branches are predicted with a GHR that does not yet include
the older outcomes. A real design updates the GHR speculatively in IF and repairs it on a
mispredict.

## 7. One difference from the sheet

The sheet's example statistics block (t1, 2bit) shows `forwards = 12`; we report 20. Counting by
hand: with 2bit, iterations 3–10 run with no bubbles, so each `add` takes `t0` from the `addi` two
instructions earlier (MEM/WB → EX), and each `bne` takes `t0` from the `addi` immediately before
it (EX/MEM → EX). That is 2 forwards per iteration and 20 in total. With the not-taken predictor
we get exactly 12, so the sheet's line looks like it came from that run. Every other line of the
block (retired 37, CPI 1.216, stalls 0, branches 10, mispredicts 2, accuracy 80.0%, redirects 2,
squashed 4) matches.
