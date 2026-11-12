# Recursive factorial with a real stack frame: prints 10! = 3628800
.text
.globl main
main:
    li   sp, 0x80000     # sp starts at 0 in this simulator: set up a stack
    li   a0, 10
    call fact
    li   a7, 1
    ecall
    li   a7, 10
    ecall
fact:                    # a0 = n -> a0 = n!
    addi sp, sp, -8
    sw   ra, 4(sp)
    sw   a0, 0(sp)
    li   t0, 1
    ble  a0, t0, base
    addi a0, a0, -1
    call fact
    lw   t1, 0(sp)       # n
    mv   t2, a0          # (n-1)!
    li   a0, 0
mulloop:                 # a0 = t1 * t2 by repeated addition (no M extension)
    beqz t1, ret_
    add  a0, a0, t2
    addi t1, t1, -1
    j    mulloop
base:
    li   a0, 1
ret_:
    lw   ra, 4(sp)
    addi sp, sp, 8
    ret
