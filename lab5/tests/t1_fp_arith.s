# Loads, stores, moves and the nine arithmetic instructions, with one
# FP load-use pair (flw -> fmul), the program's single stall under
# forwarding. Ends with fcsr = NX (1/3 is inexact).
.text
.globl main
main:
    la    a0, vals
    flw   fa0, 20(a0)           # 1.0
    flw   fs1, 4(a0)            # 2.25
    flw   fs2, 8(a0)            # 3.0
    flw   fs0, 0(a0)            # 1.5
    fmul.s fs3, fs0, fs0        # LOAD-USE: one stall, then FWD_MEM_WB  -> 2.25
    fadd.s fs4, fs1, fs2        # 5.25
    fsub.s fs5, fs2, fs1        # 0.75
    fdiv.s fs6, fs0, fs2        # 0.5
    fsqrt.s fs7, fs1            # 1.5
    fmadd.s  fs8,  fs0, fs2, fs1   #  1.5*3 + 2.25   =  6.75
    fmsub.s  fs9,  fs0, fs2, fs1   #  1.5*3 - 2.25   =  2.25
    fnmsub.s fs10, fs0, fs2, fs1   # -(1.5*3) + 2.25 = -2.25
    fnmadd.s fs11, fs0, fs2, fs1   # -(1.5*3) - 2.25 = -6.75
    fsgnj.s  ft0, fs1, fs10     # -2.25
    fmv.x.w s1, ft0             # 0xc0100000 (forwarded from EX/MEM)
    fsgnjn.s ft1, fs1, fs1      # -2.25 (fneg.s)
    fmv.x.w s2, ft1             # 0xc0100000 (forwarded from EX/MEM)
    fsgnjx.s ft2, fs10, fs10    #  2.25 (fabs.s)
    fmv.s    ft3, fs4           #  5.25
    fmv.x.w s3, ft2             # 0x40100000 (forwarded from MEM/WB)
    fsw   fs8, 12(a0)           # store 6.75
    fmv.x.w s4, ft3             # 0x40a80000 (from the register file)
    fsw   fs11, 16(a0)          # store -6.75
    lw    t0, 12(a0)            # raw bits of 6.75 = 0x40d80000
    lw    t1, 16(a0)            # raw bits of -6.75
    fmv.x.w t2, fs6             # 0x3f000000
    fmv.w.x ft4, t0             # back into the f file
    feq.s t3, ft4, fs8          # 1
    fclass.s t4, fs11           # negative normal: 1 << 1
    fdiv.s fa1, fa0, fs2        # 1/3: inexact -> NX
    fmv.x.w t5, fa1
    fmv.x.w a0, fs3             # 0x40100000
    addi  a7, zero, 10
    ecall

.data
vals: .float 1.5, 2.25, 3.0
      .word 0, 0
      .float 1.0
