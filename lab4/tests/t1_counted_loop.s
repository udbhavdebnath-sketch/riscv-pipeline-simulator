# One counted loop: the bne is taken 9 times, then falls through once.
# Expected: prints 55. 37 instructions, 10 branches (T x9, N x1).
.text
.globl main
main:
    addi t0, zero, 10       # 0x00  counter
    addi t1, zero, 0        # 0x04  sum
loop:
    add  t1, t1, t0         # 0x08
    addi t0, t0, -1         # 0x0c
    bne  t0, zero, loop     # 0x10  backward branch, target 0x08
    mv   a0, t1             # 0x14
    addi a7, zero, 1        # print 55
    ecall
    addi a7, zero, 10       # exit
    ecall
