# Collatz steps for 27: prints 111
.text
.globl main
main:
    li   t0, 27
    li   a0, 0
loop:
    li   t1, 1
    beq  t0, t1, done
    andi t2, t0, 1
    bnez t2, odd
    srli t0, t0, 1
    j    step
odd:
    slli t3, t0, 1
    add  t0, t0, t3
    addi t0, t0, 1
step:
    addi a0, a0, 1
    j    loop
done:
    li   a7, 1
    ecall
    li   a7, 10
    ecall
