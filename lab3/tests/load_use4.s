# Lab 3, Task 3: the four load-use shapes, each one stall in --pipeline-forward:
# loaded value used as an ALU operand, a branch operand, store data, and an
# address base (plus a fifth pair at the end). Expected: prints 13; 5 load-use stalls.
.text
.globl main
main:
    la   gp, data
    lw   t0, 0(gp)          # 7
    addi t1, t0, 1          # ALU operand            -> t1 = 8
    lw   t2, 4(gp)          # 0
    beq  t2, zero, ok       # branch operand (taken)
    addi t1, zero, 999      # squashed
ok:
    lw   t3, 0(gp)          # 7
    sw   t3, 12(gp)         # store data (rs2)
    lw   t4, 8(gp)          # address of the word holding 5
    lw   t5, 0(t4)          # address base (rs1)     -> t5 = 5
    lw   t6, 12(gp)         # 7, stored above
    add  a0, t6, zero
    add  a0, a0, t5         # 12
    add  a0, a0, t1         # 20
    addi a0, a0, -7         # 13
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
.data
data:
    .word 7
    .word 0
    .word five
    .word 0
five:
    .word 5
