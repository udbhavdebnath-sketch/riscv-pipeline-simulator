# Lab 5 — Floating-Point Arithmetic (RV32F)

> Historical report from before the final evaluator update. Its measured table
> and saved outputs describe that earlier revision. Use
> [final_validation.md](final_validation.md) for final submission results and
> the updated ECALL timing policy.

Raw `--batch` outputs for all four programs under `--hazard forward`, `--hazard stall` and
`--single-cycle` (fcsr line included) are in `reports/lab5_outputs/`.

**What changed, and why.** The deliverable is `isa/rv32f.c` plus `register_rv32f();` in `main.c`.
Our starter folder did not contain the infrastructure the sheet describes as given, so we wrote
it, following the sheet's descriptions:
- `core/`: `fregs`, `fcsr` and their accessors; `FMT_R2/R4/CSR`, the `IF_*` flags,
  `register_instruction_ex` and fixed-rs2 matching; rs3 and CSR-number decoding.
- `assembler/`: f registers, the optional rounding-mode operand, R4/CSR encodings, `.float`, and
  the FP/CSR pseudo-ops.
- `isa/fp_helpers.c`, `isa/zicsr.c`, `isa/rv32m.c`.
- `pipeline/`: unified register numbers, the third source, and handler-in-EX for extensions.
- `ui/`: the f-register and fcsr dump.

`rv32f.c` itself uses only that interface.

**1. Measured table.**

| Program | Instrs | forward: cycles / stalls / forwards / CPI | stall: cycles / stalls / CPI |
|---|---|---|---|
| t1_fp_arith | 36 | 41 / 1 / 11 / 1.139 | 56 / 16 / 1.556 |
| t2_conversions | 109 | 113 / 0 / 52 / 1.037 | 217 / 104 / 1.991 |
| t3_special_values | 72 | 76 / 0 / 3 / 1.056 | 80 / 4 / 1.111 |
| t4_fp_program | 133 | 159 / 14 / 59 / 1.195 | 248 / 103 / 1.865 |

Every x register, f register, fcsr and printed character matches `--single-cycle`. t4 prints
`12000014141000` and satisfies 133 + 4 + 2×4 + 14 = 159. The test programs were not in our
starter folder, so we rebuilt them from the sheet's descriptions. Instruction counts, all
forwarding-mode cycles and stalls, and the whole t1 row match the sheet. The `forwards` column
and the stall-mode column for t2–t4 differ (sheet: 47/7/61 forwards; 167/85/261 stall cycles).
Both depend only on how close together dependent instructions sit in the program text, which we
could not recover. No simulator rule is involved: the same pipeline reproduces t1 exactly.

**2. Why `pipeline/` needed no FP knowledge.**
- **Operand metadata lives in the registry, not in the instruction.** `register_instruction_ex`
  attaches `IF_RD_FP/RS1_FP/RS2_FP/RS3_FP/HAS_RM`; the 37 six-field base registrations compile
  unchanged.
- **Unified register numbers.** `control_decode` maps x1–31 → 1–31 and f0–31 → 32–63 (x0 →
  REG_NONE), so the hazard unit and the forwarding muxes compare `src1/src2/src3/dst` without
  knowing which file a register lives in. An FP dependence is simply equality of two numbers.
- **`flw`/`fsw` are just loads and stores** through MEM; only the destination file differs.

EX runs an extension's own handler on a scratch `CPUState` holding the forwarded operands.

The one thing that had to exist in advance is **the third source operand**: `src3`, its latch
value, and its forwarding mux. Without `IF_RS3_FP`, `fmadd.s` reads a stale `fs3`. That is a wrong
answer, not a wrong cycle count, and our random tester catches it (13 of 30 programs fail).

**3. Why fcsr needs no forwarding or stall.** fcsr is read and written in EX. Instructions pass
through EX one at a time, in program order, and branches also resolve in EX. So once an
instruction reaches EX it is never squashed, and it sees exactly the flags raised by every older
instruction and none raised by a younger one. The f registers, by contrast, are written in WB,
three stages after the value exists, so a consumer must be forwarded to or stalled.

If fcsr were written in WB:
- `frflags` in EX would miss the flags of up to two older FP instructions still in MEM/WB.
- A dynamic-mode instruction right after `fsrm` would use the old `frm`.

Fixing that needs either forwarding of the pending flags/frm (OR the fflags carried in EX/MEM
and MEM/WB into the value read) or an interlock that holds CSR reads and `dyn` instructions in
ID until older FP instructions drain.

One addition of ours: when an older instruction stops the machine in WB (exit `ecall` or a
fault), the one instruction behind it has already passed EX, so we restore the fcsr value it
started EX with. That keeps fcsr precise.

**4. fcvt.w.s rounding.** The exact values are 2.5, −2.5 and 3.5:
- **rne** picks the even neighbour on a tie: 2, −2, 4.
- **rtz** drops the fraction: 2, −2, 3.
- **rdn** goes toward −∞: 2, −3, 3.
- **rup** goes toward +∞: 3, −2, 4.
- **rmm** breaks ties away from zero: 3, −3, 4.

| | rne | rtz | rdn | rup | rmm |
|---|---|---|---|---|---|
| 2.5 | 2 | 2 | 2 | 3 | 3 |
| −2.5 | −2 | −2 | −3 | −2 | −3 |
| 3.5 | 4 | 3 | 3 | 4 | 4 |

t2 measures exactly this, both with the mode in the instruction and through `frm`. 2.5 and 3.5
are both ties, so rne chooses the even integer: 2 for 2.5, but 4 for 3.5. Rounding half away
from zero would give 3 and 4, and only the 2.5 case would expose the bug.

**5. min_subnormal × 1.0 vs FLT_MIN × FLT_MIN.** Underflow is raised only when a result is both
tiny and **inexact**.
- min_subnormal × 1.0 = 2⁻¹⁴⁹ is representable as-is: tiny but exact, so no flag at all.
- FLT_MIN × FLT_MIN = 2⁻²⁵² is far below the smallest subnormal and has to be rounded to +0. It
  is tiny and inexact, so UF, together with NX (an underflowed result is by definition not exact).

**6. fneg.s(sNaN) vs fadd.s(sNaN).** `fneg.s` is `fsgnjn.s fs, fs`, a sign-injection, which the
ISA defines as a bit operation. It flips bit 31, never interprets the value, so the payload
survives (`0x7fa00000 → 0xffa00000`) and nothing is raised. `fadd.s` is an arithmetic operation.
A signalling-NaN operand is an invalid operation (NV), and RISC-V requires every NaN it produces
to be the canonical `0x7fc00000`.

**7. A RARS deviation: fmadd.s(∞, 0, qNaN).** The F chapter of the RISC-V Unprivileged ISA
says: *"The fused multiply-add instructions must set the invalid operation exception flag when
the multiplicands are ∞ and zero, even when the addend is a quiet NaN."* IEEE 754 leaves this
case to the implementation. RARS takes the quiet-NaN propagation path and raises nothing; we
check for ∞×0 before the host FMA and raise NV.

How we would decide without the test suite: the ISA specification is the definition and a
simulator is only an implementation of it. Where the two disagree, the sentence in the spec
wins. A second independent implementation (Spike, or the Sail model) could act as tie-breaker.

**8. Why FP code gains so much from forwarding.** The kernels are dependence chains: almost
every instruction consumes the result of the one before it.

Newton's iteration for √2 is `fdiv → fadd → fmul → fnmsub`; the last three each read the
previous one's result at distance 1:
- Under the interlock each of those waits 2 cycles. With the loop branch (which waits on its
  counter) that is 8 stall cycles per iteration, ×4.
- With forwarding the result goes from EX/MEM straight back into EX and the chain runs at
  1 instruction per cycle.

Horner is the same shape: three chained `fmadd.s`. The dot product adds `flw → fmadd` pairs, which
even forwarding cannot fully hide: those are the 14 load-use stalls that remain.

Measured: forwarding takes t4 from 248 to 159 cycles (−36%; the sheet's version of the program:
261 → 159, −39%). In our simulator FP operations take one EX cycle like integer ones. So the size
of the gap comes from how dense the dependences are, not from FP latency. Integer loops with
similar density show similar gains: t2_nested_loops goes from 110 to 70 cycles (−36%).
