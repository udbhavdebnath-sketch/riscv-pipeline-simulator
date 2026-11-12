# Pipeline-specific FP hazards: every dependence through f registers,
# rs3, fcsr and the M extension, back to back.
.text
main:
    la    s0, k
    flw   f1, 0(s0)             # 1.5
    fmul.s f2, f1, f1           # load-use on an f register      2.25
    flw   f3, 4(s0)             # 0.1
    fmadd.s f4, f2, f1, f3      # rs3 forwarded from the load    3.475
    fsub.s f5, f4, f2, rtz      # static mode
    fsw   f5, 8(s0)             # store data forwarded from EX
    lw    t0, 8(s0)             # bits back through memory
    fmv.w.x f6, t0
    feq.s a0, f6, f5            # 1
    frflags a1                  # flags raised by the FMA (NX) right before
    fsflags zero
    li    t1, 3
    fsrm  t1                    # frm = rup, immediately used by dyn ops
    fdiv.s f7, f1, f3           # dyn -> rup
    fcvt.w.s a2, f7             # dyn -> rup
    frrm  a3                    # 3
    fsrmi 0
    li    t2, 7
    li    t3, -3
    mul   t4, t2, t3            # M extension through the pipeline: -21
    div   t5, t4, t2            # -3
    rem   t6, t4, t1            # 0
    divu  s1, t2, zero          # all ones
    fcvt.s.w f8, t4             # int result forwarded into FP
    fnmsub.s f9, f8, f1, f2     # -(f8*f1) + f2
    fnmadd.s f10, f8, f1, f2
    fmsub.s f11, f8, f1, f2
    fclass.s s2, f10
    fmax.s f12, f9, f10
    fle.s s3, f9, f10
    fsgnjx.s f13, f9, f8
    fcvt.wu.s s4, f9, rmm
    frcsr s5
    mv    a0, s4
    addi  a7, zero, 1
    ecall
    addi  a7, zero, 10
    ecall
    fdiv.s f14, f1, f0          # after the exit: its DZ must not reach fcsr
    fadd.s f15, f14, f14
.data
k:  .float 1.5, 0.1
    .word 0
