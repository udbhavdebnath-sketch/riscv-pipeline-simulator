# (lab5: RV32F) Leibniz series for pi in float32, 200 terms:
# pi ~ 4 * sum (-1)^k / (2k+1). Prints int(pi_approx * 1e6) (rtz).
.text
.globl main
main:
    li      t0, 200
    li      t1, 1
    fcvt.s.w fs0, zero           # sum = 0
    fcvt.s.w fs1, t1             # denominator d = 1
    fcvt.s.w fs2, t1             # sign = +1
    li      t2, 2
    fcvt.s.w fs3, t2             # 2.0
    fcvt.s.w fs5, t1             # 1.0
loop:
    fdiv.s  ft0, fs5, fs1        # 1/d
    fmadd.s fs0, fs2, ft0, fs0   # sum += sign * (1/d)
    fneg.s  fs2, fs2
    fadd.s  fs1, fs1, fs3        # d += 2
    addi    t0, t0, -1
    bnez    t0, loop
    li      t3, 4
    fcvt.s.w ft1, t3
    fmul.s  fs0, fs0, ft1        # pi
    li      t4, 1000000
    fcvt.s.w ft2, t4
    fmul.s  ft3, fs0, ft2
    fcvt.w.s a0, ft3, rtz
    li      a7, 1
    ecall
    li      a7, 10
    ecall
