# Binary search over a sorted table for 3 keys; prints the three indices
# (or -1): 7, -1, 0  ->  "7-10"
.text
.globl main
main:
    la   s0, tbl
    li   s1, 12          # length
    li   a0, 55
    call find
    li   a7, 1
    ecall
    li   a0, 56
    call find
    li   a7, 1
    ecall
    li   a0, 2
    call find
    li   a7, 1
    ecall
    li   a7, 10
    ecall
find:                    # a0 = key -> a0 = index or -1
    li   t0, 0           # lo
    addi t1, s1, -1      # hi
fl:
    bgt  t0, t1, nf
    add  t2, t0, t1
    srai t2, t2, 1       # mid
    slli t3, t2, 2
    add  t3, s0, t3
    lw   t4, 0(t3)
    beq  t4, a0, found
    blt  t4, a0, right
    addi t1, t2, -1
    j    fl
right:
    addi t0, t2, 1
    j    fl
found:
    mv   a0, t2
    ret
nf:
    li   a0, -1
    ret
.data
tbl: .word 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377
