# Every rounding mode, statically (in the instruction) and dynamically
# (through fcsr.frm), on the three values that tell them apart:
#           rne  rtz  rdn  rup  rmm
#   2.5      2    2    2    3    3
#  -2.5     -2   -2   -3   -2   -3
#   3.5      4    3    3    4    4
# plus the 1 + 2^-24 tie (rne -> 1.0, rmm -> 1.0000001), unsigned
# saturation and int -> float rounding. Results are stored in order at 'out'.
.text
.globl main
main:
    la    s0, k
    la    s1, out
    flw   fs0, 0(s0)            # 2.5
    flw   fs1, 4(s0)            # -2.5
    flw   fs2, 8(s0)            # 3.5
    flw   fs3, 12(s0)           # 1.0
    flw   fs4, 16(s0)           # 2^-24
    # ---- static rounding modes
    fcvt.w.s t0, fs0, rne
    sw    t0, 0(s1)
    fcvt.w.s t0, fs0, rtz
    sw    t0, 4(s1)
    fcvt.w.s t0, fs0, rdn
    sw    t0, 8(s1)
    fcvt.w.s t0, fs0, rup
    sw    t0, 12(s1)
    fcvt.w.s t0, fs0, rmm
    sw    t0, 16(s1)
    fcvt.w.s t0, fs1, rne
    sw    t0, 20(s1)
    fcvt.w.s t0, fs1, rtz
    sw    t0, 24(s1)
    fcvt.w.s t0, fs1, rdn
    sw    t0, 28(s1)
    fcvt.w.s t0, fs1, rup
    sw    t0, 32(s1)
    fcvt.w.s t0, fs1, rmm
    sw    t0, 36(s1)
    fcvt.w.s t0, fs2, rne
    sw    t0, 40(s1)
    fcvt.w.s t0, fs2, rtz
    sw    t0, 44(s1)
    fcvt.w.s t0, fs2, rdn
    sw    t0, 48(s1)
    fcvt.w.s t0, fs2, rup
    sw    t0, 52(s1)
    fcvt.w.s t0, fs2, rmm
    sw    t0, 56(s1)
    # ---- dynamic: the same conversions with no rm operand, frm set by fsrmi
    fsrmi 0                     # frm = rne
    fcvt.w.s t0, fs0
    sw    t0, 60(s1)
    fcvt.w.s t0, fs1
    sw    t0, 64(s1)
    fcvt.w.s t0, fs2
    sw    t0, 68(s1)
    fsrmi 1                     # frm = rtz
    fcvt.w.s t0, fs0
    sw    t0, 72(s1)
    fcvt.w.s t0, fs1
    sw    t0, 76(s1)
    fcvt.w.s t0, fs2
    sw    t0, 80(s1)
    fsrmi 2                     # frm = rdn
    fcvt.w.s t0, fs0
    sw    t0, 84(s1)
    fcvt.w.s t0, fs1
    sw    t0, 88(s1)
    fcvt.w.s t0, fs2
    sw    t0, 92(s1)
    fsrmi 3                     # frm = rup
    fcvt.w.s t0, fs0
    sw    t0, 96(s1)
    fcvt.w.s t0, fs1
    sw    t0, 100(s1)
    fcvt.w.s t0, fs2
    sw    t0, 104(s1)
    fsrmi 4                     # frm = rmm
    fcvt.w.s t0, fs0
    sw    t0, 108(s1)
    fcvt.w.s t0, fs1
    sw    t0, 112(s1)
    fcvt.w.s t0, fs2
    sw    t0, 116(s1)
    frrm  t1                      # 4 (rmm)
    sw    t1, 120(s1)
    fsrmi 0
    # ---- the RMM tie: 1 + 2^-24 is exactly halfway between two floats
    fadd.s ft0, fs3, fs4, rne
    fmv.x.w t0, ft0
    sw    t0, 124(s1)
    fadd.s ft0, fs3, fs4, rmm
    fmv.x.w t0, ft0
    sw    t0, 128(s1)
    # ---- unsigned: negative saturates to 0 with NV only
    fcvt.wu.s t0, fs1, rne
    sw    t0, 132(s1)
    fcvt.wu.s t0, fs1, rtz
    sw    t0, 136(s1)
    fcvt.wu.s t0, fs1, rup
    sw    t0, 140(s1)
    fcvt.wu.s t0, fs2, rmm        # 4
    sw    t0, 144(s1)
    fcvt.wu.s t0, fs0, rup        # 3
    sw    t0, 148(s1)
    # ---- int -> float: 2^24 + 1 needs 25 bits, rounds and raises NX
    li    t2, 16777217
    fcvt.s.w ft1, t2, rne
    fmv.x.w t0, ft1
    sw    t0, 152(s1)
    fcvt.s.w ft1, t2, rup
    fmv.x.w t0, ft1
    sw    t0, 156(s1)
    li    t3, -1
    fcvt.s.wu ft2, t3             # 4294967295 -> 2^32
    fmv.x.w t0, ft2
    sw    t0, 160(s1)
    frflags a0                    # accrued: NV | NX
    addi  a7, zero, 10
    ecall

.data
k:   .float 2.5, -2.5, 3.5, 1.0
     .word 0x33800000              # 2^-24
out:  .word 0
