# The special-value reference table, one case per group, each flag isolated:
# clear fflags, run the operation, read fflags back. Results stay in
# registers so the final dump shows them all; the case's flags are in the
# register named in its comment.
.text
.globl main
main:
    la    s0, k
    flw   ft1, 0(s0)          # 0.0 (+0)
    flw   ft2, 4(s0)          # 1.0
    flw   ft3, 8(s0)          # FLT_MAX
    flw   ft4, 12(s0)         # FLT_MIN
    flw   ft5, 16(s0)         # min subnormal
    flw   ft6, 20(s0)         # +inf
    flw   ft7, 24(s0)         # qNaN
    flw   ft8, 28(s0)         # sNaN
    flw   ft9, 32(s0)         # -1.0
    flw   ft10, 36(s0)        # -0.0
    fsflags zero
    fdiv.s fs0, ft1, ft1          # 0.0 / 0.0        -> canonical NaN, NV
    frflags s1                    # flags of case 1
    fsflags zero
    fdiv.s fs1, ft2, ft1          # 1.0 / 0.0        -> +inf, DZ
    frflags s2                    # flags of case 2
    fsflags zero
    fadd.s fs2, ft3, ft3          # FLT_MAX+FLT_MAX  -> +inf, OF NX
    frflags s3                    # flags of case 3
    fsflags zero
    fmul.s fs3, ft4, ft4          # FLT_MIN*FLT_MIN  -> +0, UF NX
    frflags s4                    # flags of case 4
    fsflags zero
    fmul.s fs4, ft5, ft2          # min_sub * 1.0    -> min_sub, none (exact)
    frflags s5                    # flags of case 5
    fsflags zero
    fsub.s fs5, ft6, ft6          # inf - inf        -> canonical NaN, NV
    frflags s6                    # flags of case 6
    fsflags zero
    fadd.s fs6, ft7, ft2          # qNaN + 1.0       -> canonical NaN, none
    frflags s7                    # flags of case 7
    fsflags zero
    fadd.s fs7, ft8, ft2          # sNaN + 1.0       -> canonical NaN, NV
    frflags s8                    # flags of case 8
    fsflags zero
    fsqrt.s fs8, ft9              # sqrt(-1.0)       -> canonical NaN, NV
    frflags s9                    # flags of case 9
    fsflags zero
    fmadd.s fs9, ft6, ft1, ft7    # fmadd(inf,0,qNaN)-> canonical NaN, NV (RISC-V rule)
    frflags s10                   # flags of case 10
    fsflags zero
    fmin.s fs10, ft7, ft2         # fmin(qNaN, 1.0)  -> 1.0, none
    frflags s11                   # flags of case 11
    fsflags zero
    fmin.s fs11, ft8, ft2         # fmin(sNaN, 1.0)  -> 1.0, NV
    frflags t0                    # flags of case 12
    fsflags zero
    fmin.s fa1, ft7, ft7          # fmin(qNaN, qNaN) -> canonical NaN, none
    frflags t1                    # flags of case 13
    fsflags zero
    fmin.s fa2, ft10, ft1         # fmin(-0.0, +0.0) -> -0.0, none
    frflags t2                    # flags of case 14
    fsflags zero
    fmax.s fa3, ft10, ft1         # fmax(-0.0, +0.0) -> +0.0, none
    frflags t3                    # flags of case 15
    fsflags zero
    feq.s a2, ft7, ft7            # feq(qNaN, qNaN)  -> 0, none (quiet compare)
    frflags t4                    # flags of case 16
    fsflags zero
    flt.s a3, ft7, ft2            # flt(qNaN, 1.0)   -> 0, NV
    frflags t5                    # flags of case 17
    fsflags zero
    feq.s a4, ft8, ft2            # feq(sNaN, 1.0)   -> 0, NV
    frflags t6                    # flags of case 18
    fsflags zero
    fneg.s fa4, ft8               # fneg(sNaN)       -> 0xffa00000, none (bit op)
    frflags a1                    # flags of case 19
    frcsr a5                        # all flags of the last case: none
    addi  a7, zero, 10
    ecall

.data
k:  .float 0.0, 1.0
    .word 0x7f7fffff, 0x00800000, 0x00000001, 0x7f800000   # FLT_MAX FLT_MIN min_sub +inf
    .word 0x7fc00000, 0x7fa00000                           # qNaN sNaN
    .float -1.0, -0.0
