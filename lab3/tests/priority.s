# Lab 3 priority rule: EX/MEM (the younger producer) must win over MEM/WB.
# Expected: t1 = 2 (prints 2). The wrong priority gives 1.
.text
.globl main
main:
    addi t0, zero, 1
    addi t0, t0, 1      # t0 = 2 ; sits in EX/MEM when the next instruction is in EX
    addi t1, t0, 0      # must see 2
    mv   a0, t1
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
