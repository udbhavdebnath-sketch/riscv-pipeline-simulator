# RISC-V Simulator — Implementation Plan

## Status
- **Lab 2: DONE** (`lab2/`, tag `lab2-done`). All 3 rows of the W7 table match exactly
  (108/40/11, 26/8/1, 44/19/3), 5×addi example = 9 cycles / CPI 1.80, registers + output match
  the oracle on all samples and edge tests (`lab2/compare.sh`). Stalls/flushes are counted when the
  bubble reaches WB, so `cycles = retired+stalls+flushes+4` holds for every program (incl. faults:
  the faulting cycle is not counted). `tests/sample3_hazards.s` reconstructed to hit 18/44/19/3.
- No further material is coming: Labs 3–5 are built entirely from the sheets (decision 2026-11-12).
- **Lab 3: DONE** (`lab3/`, tag `lab3-done`). Plug-in hazard unit (`pipeline/hazard.[ch]`,
  `--hazard forward|stall`, default forward). Forward: EX/MEM + MEM/WB → EX, 1 stall on load-use
  (rs1 or rs2, incl. store data). Stall: interlock on ID/EX and EX/MEM (register file is
  write-before-read). Fetch always pc+4; taken branch / jal / jalr squash 2 → `cycles =
  retired + 4 + 2×redirects + stalls` holds on every run. `--batch` = pipeline,
  `--single-cycle` = oracle, `--pipeline` kept as alias, `--trace` matches the Lab 4 sheet format.
  Rebuilt `tests/t1..t4` hit the Lab 4 "Lab 2" column exactly: 59 / 78 / 84 / 344, and t1
  forwards = 12. `lab3/compare.sh` = oracle diff under both hazard units.
- **Lab 4: DONE** (`lab4/`, tag `lab4-done`). `branch_predictor.[ch]` (interface verbatim from
  the sheet + `bp_find`), `bp_static.c` (not-taken, btfn), `bp_dynamic.c` (1bit, 2bit start=1).
  IF pre-decodes via `decode_instruction`: cond. branch → `predict`, jal → free redirect, jalr →
  pc+4. EX updates with the real outcome, redirects when real next ≠ `pred_next`.
  Both §9 tables reproduced cell-for-cell (cycles + mispredicts, all 16), trace cycles 7/8/11/12/13
  identical to the sheet, stats block identical except `forwards` (we get 20 for t1/2bit; the
  sheet's 12 is the not-taken value — we count each forwarded operand). `make test` = 56 runs
  (7 programs × 2 hazard × 4 predictors) all match the oracle, both identities hold.
- **Lab 5: DONE** (`lab5/`, tag `lab5-done`). Wrote all the "given" infrastructure ourselves:
  `fregs`/`fcsr` + accessors in `core/cpu_state`, `FMT_R2/R4/CSR` + `IF_*` flags +
  `register_instruction_ex` + rs2-fixed matching in the registry/decoder, assembler support
  (f registers, optional rm operand, R4/CSR encodings, `.float`, `la`, FP/CSR pseudo-ops and
  common RV32I pseudo-ops: beqz/bnez/bgt/ble/call/tail/neg/not/seqz/...), `isa/fp_helpers.c`
  (memcpy bit casts, resolve_rm, host_mode, fflags_of, d2f + RMM tie fix-up, fp_arith with
  canonical NaN and the RISC-V inf×0 rule), `isa/zicsr.c`, `isa/rv32m.c`, and the student file
  `isa/rv32f.c` (26 instructions). Pipeline: unified register numbers (x1–31 → 1–31,
  f0–31 → 32–63) drive the hazard unit; extension instructions run their own handler in EX on a
  scratch CPUState with forwarded operands; fcsr read/written in EX (and restored if an older
  instruction stops the machine in WB). Batch dump now also prints f0–f31 and `fcsr`.
  Verified: all 19 rows of the special-value appendix, the 10 fcvt rounding values (static and
  dyn), the RMM 1+2^-24 tie, saturation/NV-only rules, reserved rm → precise ILLEGAL.
  `make test` = 144 runs (18 programs × 2 hazard × 4 predictors) all match the oracle in x, f and
  fcsr. Rebuilt FP tests hit the forward columns exactly (36/41/1, 109/113/0, 72/76/0,
  133/159/14 + prints 12000014141000); t1 matches its whole row incl. forwards 11 and
  stall-mode 56/16. Differences, all from our reconstructed programs' instruction order rather
  than simulator policy: forwards t2 52 vs 47, t3 3 vs 7, t4 59 vs 61; stall-mode cycles
  t2 217 vs 167, t3 80 vs 85, t4 248 vs 261.
  Makefile: `-MMD -MP` header deps (macOS make has 1-second timestamps: use `make -B` after quick edits).

- **Assembler backport (step 1 after Lab 5): DONE.** Labs 2–4 assemblers now accept `la` and the
  RV32I pseudo-ops (beqz bnez blez bgez bltz bgtz bgt ble bgtu bleu neg not seqz snez sltz sgtz
  jr call tail, 1-operand jal/jalr), same table as Lab 5. `tests/e_pseudo.s` checks all 20.
  Regression: lab2 8/8, lab3 24/24, lab4 96/96, lab5 152/152 runs match the oracle; all
  reference tables unchanged.

- **Step 2 — stress testing: DONE.**
  - `tests/fuzz.py <lab> <n> [seed] [--fp]`: random, always-terminating programs (dependent ALU
    chains, every load/store width, load-use incl. store data and FMA rs3, forward branches incl.
    pseudo-branches, nested counted loops, call / jal / indirect jalr, prints, x0 writes; with
    --fp: M ops, all 26 F instructions on special values, every rounding mode incl. frm changes,
    flag CSRs). Each program goes through the lab's compare.sh (every hazard × predictor vs the
    oracle). 500 programs per lab = 9,500 runs, 0 failures. Mutation check: dropping MEM/WB
    forwarding (lab4) fails 17/30 programs, dropping IF_RS3_FP (lab5) fails 13/30.
  - `golden.sh`: every published figure from the sheets (66 checks) — guards the timing policy,
    which the fuzzer cannot see (a 2-latch Lab 2 interlock is still correct, just not the sheet's).
  - `tests/e_control.s` (jump-to-jump, odd jalr target, jal x0, back-to-back taken branches, jalr
    to pc+4, unknown ecall) and `tests/e_store_fault.s`: match in every lab.
  - Run everything: `./golden.sh` and `make test` in each lab; `python3 tests/fuzz.py lab5 200 --fp`.

## Known limits / report notes
- A jalr whose target is pc+4 is not redirected in Labs 4/5 (IF's pc+4 guess was right), so
  `redirects = mispredicts + jalr` becomes `<=` for such programs; the cycle identity still holds.
- Self-modifying code (a store into an instruction already fetched) would run the stale
  instruction in the pipeline but not in the oracle. Not handled; no lab asks for it.
- The base assembler silently truncates immediates that don't fit (e.g. `ori x, x, 0x4000`).
  Left as is: it is the handout's behaviour and the oracle shares it.
- Lab 4 sheet's t1/2bit stats block says forwards = 12; we count 20 per forwarded operand
  (12 is what the not-taken run gives). Cycles and all other counters match.
- Host is arm64: tininess for UF is detected by the host FPU; RISC-V detects after rounding.
  Only differs for results that round up to exactly FLT_MIN.
- Lab 3 "stall" interlock = ID/EX + EX/MEM (register file write-before-read). The 3-latch
  version (Lab 2's) gives t4 301 cycles vs the sheet's 261, so 2-latch is the closer reading.

Evaluation: **Nov 16–19 2026**, group of 2, no makeup.
Marks: **4 labs × 5 hidden test cases = 20**. Graders will run their own `.s` programs
with each lab sheet's exact command lines and compare registers, printed output and
statistics. So the priorities are: (1) exact correctness against the single-cycle oracle on
*any* program, (2) cycle/stall/flush counts that follow each sheet's stated policy exactly,
(3) CLI flags and output format exactly as written in the sheets.

| Week | Topic | Source in this folder | Graded? |
|---|---|---|---|
| 1 | Simulator basics | `W6LAB.zip` (base code, manual, slides) | no (intro) |
| 2 | 5-stage pipeline | `W7_RISCV_Pipeline_Lab_Sheet.pdf` | yes |
| 3 | Hazard detection + forwarding | **no sheet in folder** | yes |
| 4 | Branch prediction | `Lab Sheet 4 …Branch Prediction….pdf` | yes |
| 5 | RV32F floating point | `Lab Sheet 5 …RV32F….pdf` | yes |

---

## 0. What is in the folder, and what is missing

Present and verified:
- `W6LAB.zip → riscv_sim_base.zip`: working single-cycle RV32I sim (~1.9k lines C).
  Builds clean; `sample1` → prints 55, `t1=0x37`, 53 cycles; `sample2` → 13 cycles. ✔
- Manual (`RISC-V_Simulator_Codebase_Manual.md`) and slide deck: conceptual only, nothing to implement.

**Missing — must be obtained or reconstructed:**

| Missing item | Needed for | Plan if we can't get it |
|---|---|---|
| W7 sheet Tasks 4, 5, 6 + Appendix A (`sample3_hazards.s`). The PDF jumps from Task 3 to §7; page 6 is blank. | Lab 2 | Infer from §7 numbers (see §2 below); write our own `sample3_hazards.s` |
| Entire Lab 3 sheet (hazard/forwarding) | Lab 3 | Infer from Lab 4/5 references (`--hazard stall|forward`, `forwards` counter, FWD_EX_MEM/FWD_MEM_WB, load-use = 1 stall) |
| Lab 4 "given" files: `branch_predictor.h/.c`, the predictor-aware `pipeline.c`, tests `t1_counted_loop.s`…`t4_calls.s`, golden files, `make test` | Lab 4 | Write them ourselves from the interface text in the sheet; rebuild the 4 tests to hit the published counts |
| Lab 5 "given" files: fregs/fcsr in `cpu_state`, `register_instruction_ex`, FMT_R2/R4/CSR, decoder rs3/csr, assembler f-regs/`.float`/rm operand/FP pseudo-ops, `fp_helpers.c`, `zicsr.c`, `rv32m.c`, tests `t1_fp_arith.s`…`t4_fp_program.s` | Lab 5 | Write all of it ourselves (largest chunk of work) |

➡ **Action today:** ask the TA / classmates for the Lab 3 sheet, the complete W7 sheet, and
the lab3/lab4/lab5 starter folders. Every given file we can get is code we don't have to
write *and* guarantees our output format matches the grader's. Everything below assumes the
worst case (we get nothing).

---

## 1. Structure decision: one folder per lab

The sheets contradict each other on CLI semantics, so one binary can't satisfy all of them:

| | Lab 2 (W7) | Labs 4 & 5 |
|---|---|---|
| `--batch` alone | single-cycle | pipeline (forward + 2bit) |
| pipeline flag | `--pipeline` | default |
| oracle flag | (none — `--batch` is it) | `--single-cycle` |
| taken branch / jump cost | **1 cycle** (IF fetches `redirect_pc` the same cycle EX resolves) | **2 cycles** (IF and ID squashed, refetch next cycle) |
| stall policy | naive 3-latch stall, no forwarding | `--hazard forward` (default) or `stall` |

And the sheets themselves say `cd lab3`, `cd lab5`. So:

```
riscv/
  lab2/  lab3/  lab4/  lab5/     # each a full copy of riscv_sim_base + that lab's work
  tests/                         # our shared extra test programs + compare script
```
Each lab is frozen once its numbers match, then copied forward as the next lab's start.
`git init` at `riscv/`, tag each frozen lab.

---

## 2. Lab 2 — Five-stage pipeline (`lab2/`)

New files only in `pipeline/` (+3 lines `main.c`, +2 lines `Makefile`):

| File | Content |
|---|---|
| `pipeline/control.h/.c` | `WbSrc`, `ControlSignals`, `control_decode()` — one switch on `h->format`, exactly the sheet's table |
| `pipeline/pipeline_state.h` | `IFID`, `IDEX`, `EXMEM`, `MEMWB`, `Pipeline{…, cycles, retired, stalls, flushes}`, `PF_*` fault codes |
| `pipeline/pipeline.c` | `pipeline_init`, `pipeline_cycle` (snapshot `nx = *p` … `*p = nx`), `pipeline_run_batch`, `static alu()` |

Stage rules (from the sheet, condensed):
- **WB**: fault → set `cpu->status` and stop (precise exceptions); `reg_write` → `cpu_write_reg`;
  `is_system` → set `cpu->pc` to instr pc, call `handler->execute`, restore; `retired++`.
- **MEM**: loads/stores by `funct3`; memory error → `PF_MEM` in latch, never halt.
- **EX**: branch compare on latched `rs1_val/rs2_val` → flush + `redirect_pc`; jal/jalr →
  `alu_result = pc+4`, flush, target; else `alu()` (lui=imm, auipc=pc+imm, ld/st = a+imm, shifts `&0x1F`).
- **ID**: `decode_instruction`; unknown → `PF_ILLEGAL` (don't touch `h`); propagate `PF_FETCH`;
  `control_decode`; hazard check → stall; else read both regs.
- **IF**: fetch from `redirect_pc` if flushing this cycle else `cpu->pc`; fault → `PF_FETCH`; `cpu->pc = fetch_pc + 4`.

Reconstructed Tasks 4–6 (missing pages), derived from §7's numbers:
- **Task 4 hazard ("naive 3-latch stalling")**: stall in ID if a used `rs1/rs2 ≠ 0` equals the
  `rd` of a *valid, reg_write* instruction in ID/EX, EX/MEM **or** MEM/WB. On stall: freeze IF/ID
  and `cpu->pc`, inject a bubble into ID/EX, `stalls++`.
- **Task 5 flush**: EX raises flush → kill the instruction in ID (bubble into ID/EX), IF fetches
  `redirect_pc` the same cycle. Cost = 1 cycle, `flushes++` once. (This is what makes
  `cycles = retired + stalls + flushes + 4` hold with 11 flushes on sample1.) Flush beats stall.
- **Task 6**: `main.c` `--pipeline` → `pipeline_init` + `pipeline_run_batch` + stats print;
  Makefile adds `-Ipipeline` and the two `.c` files.

Batch output for `--pipeline`: same register dump as `run_batch` + `cycles / retired / stalls / flushes / CPI`.

**Acceptance (must match exactly):**

| Program | single | pipe cycles | retired | stalls | flushes | CPI |
|---|---|---|---|---|---|---|
| sample1_sum_loop | 53 | 108 | 53 | 40 | 11 | 2.038 |
| sample2_mem_and_branches | 13 | 26 | 13 | 8 | 1 | 2.000 |
| sample3_hazards (we write it) | 18 | 44 | 18 | 19 | 3 | 2.444 |

+ the 5×`addi` appendix example → 9 cycles, CPI 1.80. If stalls are off by a few, adjust whether
MEM/WB is in the compare set (that is the only real knob) until the table matches.

---

## 3. Lab 3 — Hazard detection unit + forwarding (`lab3/`)

No sheet; behaviour pinned down by what Labs 4/5 assume:
- Plug-in hazard unit selected with `--hazard forward|stall` (default `forward`), shaped like
  `HazardDetector` function-pointer struct (README mentions `hazard_interface.h`,
  `hazard_naive.c`, `hazard_forwarding.c`).
- Forwarding: EX/MEM → EX and MEM/WB → EX for rs1/rs2 (`FWD_NONE/FWD_EX_MEM/FWD_MEM_WB`),
  counter `forwards`. Load-use → exactly **1 stall** then `FWD_MEM_WB`.
- Branches resolved in EX with forwarded operands; mispredict/jump costs **2 cycles**
  (IF and ID squashed). Identity: `cycles = retired + 4 + 2×redirects + stalls`.
- Add `--single-cycle`, `--trace` (`[cycle N] IF 0x… | ID ---------- | EX … | MEM … | WB …`).
- Design unified register numbers now (x1–31 → 1–31, f0–31 → 32–63, x0 = `REG_NONE`) and
  `src1/src2/src3/dst` + `fwd3` in Control — Lab 5 says these were "paid for in advance".

**Acceptance:** Lab 4 table column "Lab 2" = this pipeline with no IF jal pre-decode:
t1 59, t2 78, t3 84, t4 344 cycles; zero stalls on t1 under forward; registers match oracle.
Also all Lab 2 programs match oracle registers under both hazard units.

---

## 4. Lab 4 — Branch prediction (`lab4/`)

Write the "given" infrastructure first, exactly as specified in the sheet:
- `pipeline/branch_predictor.h`: `BP_TABLE_BITS 6`, `BPState{uint8_t table[64]}`,
  `bp_index(pc) = (pc>>2)&63`, `BranchPredictor{name, description, reset, predict, update}`.
- `pipeline/branch_predictor.c`: table of the 4 predictors + `bp_find(name)`.
- `pipeline.c` changes: IF pre-decodes fetched word — cond. branch → `predict(pc,target)`;
  `jal` → redirect in IF (free); else pc+4. Carry `pred_next` down the pipe. EX: `update()` for
  cond. branches; if real next PC ≠ `pred_next` → squash IF+ID, refetch (2 cycles). `jalr`
  never predicted (always 2 cycles).
- Student files: `bp_static.c` (not-taken, btfn) and `bp_dynamic.c` (1bit, 2bit — saturating,
  start at 1 = weakly-not-taken). Code is given verbatim in the sheet.
- `--predictor not-taken|btfn|1bit|2bit` (default `2bit`); stats block exactly as in §8 of the sheet
  (`pipeline =`, `retired`, `CPI`, `stalls`, `forwards`, `branches`, `mispredicts`, `accuracy`,
  `jal`, `jalr`, `redirects`, `squashed`).
- Rebuild `tests/t1_counted_loop.s … t4_calls.s` so instruction/branch/jal/jalr counts match the
  sheet; add `make test` that diffs registers vs `--single-cycle` for every predictor.

**Acceptance:** both cycle tables of §9 exactly (e.g. t1: 59/43/45/45; t4: 278/278/280/278;
mispredicts 9/1/2/2 …), identities `cycles = retired+4+2×redirects+stalls`,
`redirects = mispredicts + jalr` on every run.

---

## 5. Lab 5 — RV32F floating point (`lab5/`)

Biggest chunk. Infrastructure we must write first (sheet calls it "given"):
1. `core/cpu_state`: `fregs[32]`, `fcsr`, `cpu_read_freg/cpu_write_freg`, `cpu_raise_fflags`,
   `cpu_frm`; batch dump prints `f` registers and `fcsr =0x… (frm=N flags: NX …)`.
2. `core/isa_registry`: formats `FMT_R2`, `FMT_R4`, `FMT_CSR`; flags `IF_RD_FP, IF_RS1_FP,
   IF_RS2_FP, IF_RS3_FP, IF_HAS_RM, IF_RS2_FIXED`; `register_instruction_ex(handler, flags, rs2_sel)`.
   Old 6-field `register_instruction` calls keep compiling.
3. `core/decoder`: `rs3 = raw>>27`, CSR number `raw>>20`, fixed-rs2 matching; funct3 = don't-care when `IF_HAS_RM`.
4. `assembler`: `f0–f31` + ABI names (`ft0–11, fs0–11, fa0–7`), optional trailing rounding-mode
   operand (`rne rtz rdn rup rmm dyn`, default dyn=7), `.float`, R4 encoding, pseudo-ops
   `fmv.s fneg.s fabs.s`, `frflags fsflags frrm fsrm frcsr fscsr`, `csrr/csrw` etc.
5. `isa/zicsr.c` (csrrw/s/c + immediate forms on fflags 0x001, frm 0x002, fcsr 0x003),
   `isa/rv32m.c` (mul/mulh/mulhsu/mulhu/div/divu/rem/remu, RISC-V div-by-zero/overflow rules),
   `isa/fp_helpers.c` (`f_of/bits_of` via memcpy, `resolve_rm`, `host_mode`, `fflags_of`, `d2f`
   with RMM tie fix-up, `fp_arith` with canonical NaN `0x7fc00000`). Makefile: `-frounding-math -lm`.
6. Pipeline: ID reads f regs per flags, EX runs the handler on a scratch `CPUState` with
   latched/forwarded operands, WB writes `fd`; hazard unit already works on unified numbers.
   fcsr read/written in EX (no hazard logic needed).

Then the actual student file `isa/rv32f.c` (26 instructions) in the sheet's task order:
- T1 bits only: flw, fsw, fsgnj/n/x, fmv.x.w, fmv.w.x, fclass.s
- T2 arithmetic: fadd/fsub/fmul/fdiv/fsqrt, fmadd/fmsub/fnmsub/fnmadd (sign-flip table, `fmaf`, `IF_RS3_FP`)
- T3 RISC-V≠C: feq (quiet), flt/fle (signalling), fmin/fmax (NaN + ±0 rules), fcvt.w[u].s
  (round in double, saturate, NV/NX), fcvt.s.w[u]
- T4 flags: sticky NV/DZ/OF/UF/NX; UF only if inexact; illegal rm 5/6/bad frm → ILLEGAL, no writes.

**Acceptance:**

| Program | Instrs | forward cyc/stalls/fwds/CPI | stall cyc/stalls/CPI |
|---|---|---|---|
| t1_fp_arith | 36 | 41 / 1 / 11 / 1.139 | 56 / 16 / 1.556 |
| t2_conversions | 109 | 113 / 0 / 47 / 1.037 | 167 / 54 / 1.532 |
| t3_special_values | 72 | 76 / 0 / 7 / 1.056 | 85 / 9 / 1.181 |
| t4_fp_program | 133 | 159 / 14 / 61 / 1.195 | 261 / 116 / 1.962 |

t4 prints `12000014141000`; fcvt table (2.5/-2.5/3.5 × 5 modes); the 19-row special-value appendix.
x, f **and fcsr** identical between `--single-cycle` and pipeline.

---

## 6. Verification harness (shared, built in Lab 2, grows each lab)

- `tests/compare.sh`: for every `.s` × every mode, diff `^x` (and `^f`, `fcsr`) lines + program
  stdout against the single-cycle oracle; check the cycle identity; print MISMATCH lines only.
- Our own stress programs, since graders use hidden tests: back-to-back RAW chains, load-use,
  store-after-load, branch on just-loaded value, jal/jalr/ret chains, x0 as rd, ecall right
  after a dependent instruction, out-of-bounds load (precise fault), falling into `.data`
  (must not fault before the final ecall retires), negative offsets, sb/sh/lb/lhu sign cases.
- Optional cross-check against RARS if installed (Lab 5 lists 4 places RARS is wrong — trust the sheet).
- `-Wall -Wextra` warning-free at every step.

---

## 7. Schedule (today = Thu Nov 12, eval from Mon Nov 16)

| Day | Work | Owner split (2 people) |
|---|---|---|
| Thu 12 | Ask for missing sheets/starters. Repo + lab2 skeleton, control.c, latches | A: pipeline.c stages · B: compare.sh + test programs |
| Fri 13 | Lab 2 matching table → freeze. Lab 3 hazard/forwarding + `--trace` | A: forwarding unit · B: Lab 4 predictor files + t1–t4 tests |
| Sat 14 | Lab 3 freeze; Lab 4 IF pre-decode + predictors → match both tables | A: Lab 5 core/decoder/assembler infra · B: Lab 4 verification |
| Sun 15 | Lab 5 rv32f.c T1–T4, fp_helpers, zicsr, rv32m; match FP table | A+B |
| Mon 16 | Buffer: stress tests on all 4 labs, report numbers/answers | A+B |

Risk order: Lab 5 infra is the largest unknown; Lab 3 has no spec — if its sheet turns up,
re-check everything in §3 against it first.

---

## 8. Open questions (answer before / while coding)

1. Can you get the Lab 3 sheet, W7 pages for Tasks 4–6 + Appendix A, and the lab3/4/5 starter
   folders? (Changes §3–§5 from "reconstruct" to "drop in".)
2. Per-lab folders (recommended, §1) vs one cumulative binary — confirm with the TA which
   commands the evaluation will run for each week.
3. Do the reports (Lab 4/5 "Report questions") count, or only the 5 test cases per lab?
