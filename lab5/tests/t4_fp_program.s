# Three FP kernels, each a dependence chain:
#   1. dot product of two 14-element vectors (unrolled x2)   = 120
#   2. Newton's method for sqrt(2), 4 iterations              = 1.4142135
#   3. Horner evaluation of 0.125x^3 + 0.125x^2 + 0.25x + 0.5 at x = 1 = 1.0
# Each result is scaled by 1000, converted, and printed: 12000014141000
.text
.globl main
main:
    la    a0, vec_a
    la    a1, vec_b
    li    t0, 7                 # 7 iterations x 2 elements
    fmv.w.x fs0, zero           # acc = 0.0
dot:
    flw   ft0, 0(a0)
    flw   ft1, 0(a1)
    fmadd.s fs0, ft0, ft1, fs0  # load-use: one stall
    flw   ft2, 4(a0)
    flw   ft3, 4(a1)
    fmadd.s fs0, ft2, ft3, fs0  # load-use: one stall
    addi  a0, a0, 8
    addi  a1, a1, 8
    addi  t0, t0, -1
    bne   t0, zero, dot

    la    a2, consts
    flw   fs1, 0(a2)            # 2.0
    flw   fs2, 4(a2)            # 0.5
    flw   fs3, 8(a2)            # 1000.0
    fmv.s fs4, fs2              # x0 = 0.5 * 2 ...
    fadd.s fs4, fs4, fs2        # ... = 1.0
    fmv.s fs6, fs1              # residual starts at 2
    li    t1, 4
newton:                         # x = 0.5 * (x + 2/x)
    fdiv.s ft4, fs1, fs4
    fadd.s ft4, fs4, ft4
    fmul.s fs4, ft4, fs2
    fnmsub.s fs6, fs4, fs4, fs1 # residual 2 - x*x, shrinks each pass
    addi  t1, t1, -1
    bne   t1, zero, newton

    la    a3, poly
    flw   ft9, 16(a3)           # x
    flw   ft5, 0(a3)            # c3
    flw   ft6, 4(a3)            # c2
    flw   ft7, 8(a3)            # c1
    flw   ft8, 12(a3)           # c0
    fmadd.s fs5, ft5, ft9, ft6  # c3*x + c2
    fmadd.s fs5, fs5, ft9, ft7  # (..)*x + c1
    fmadd.s fs5, fs5, ft9, ft8  # (..)*x + c0

    fmul.s ft10, fs0, fs3
    fcvt.w.s a0, ft10
    addi  a7, zero, 1
    ecall                       # 120000
    fmul.s ft10, fs4, fs3
    fcvt.w.s a0, ft10
    ecall                       # 1414
    fmul.s ft10, fs5, fs3
    fcvt.w.s a0, ft10
    ecall                       # 1000
    addi  a7, zero, 10
    ecall

.data
vec_a: .float 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
vec_b: .float 16, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
consts: .float 2.0, 0.5, 1000.0
poly:  .float 0.125, 0.125, 0.25, 0.5, 1.0
