# (lab5: M extension) 3x3 integer matrix multiply C = A*B, then prints the
# trace of C and gcd(trace, 360) using rem.
.text
.globl main
main:
    la   s0, A
    la   s1, B
    la   s2, C
    li   s3, 3
    li   t0, 0                   # i
li_:
    li   t1, 0                   # j
lj:
    li   t2, 0                   # k
    li   a0, 0                   # acc
lk:
    mul  t3, t0, s3
    add  t3, t3, t2
    slli t3, t3, 2
    add  t3, s0, t3
    lw   t4, 0(t3)               # A[i][k]
    mul  t5, t2, s3
    add  t5, t5, t1
    slli t5, t5, 2
    add  t5, s1, t5
    lw   t6, 0(t5)               # B[k][j]
    mul  t4, t4, t6
    add  a0, a0, t4
    addi t2, t2, 1
    blt  t2, s3, lk
    mul  t3, t0, s3
    add  t3, t3, t1
    slli t3, t3, 2
    add  t3, s2, t3
    sw   a0, 0(t3)
    addi t1, t1, 1
    blt  t1, s3, lj
    addi t0, t0, 1
    blt  t0, s3, li_
    lw   a0, 0(s2)               # trace = C00 + C11 + C22
    lw   t0, 16(s2)
    add  a0, a0, t0
    lw   t0, 32(s2)
    add  a0, a0, t0
    li   a7, 1
    ecall
    li   a1, 360                 # gcd(trace, 360) by Euclid with rem
g:
    beqz a1, gd
    rem  t0, a0, a1
    mv   a0, a1
    mv   a1, t0
    j    g
gd:
    ecall
    li   a7, 10
    ecall
.data
A: .word 1, -2, 3, 4, 5, -6, 7, 8, 9
B: .word 9, 8, -7, 6, 5, 4, 3, -2, 1
C: .space 36
