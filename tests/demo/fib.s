# Iterative Fibonacci: prints fib(25) = 75025
.text
.globl main
main:
    li   t0, 25          # n
    li   a0, 0           # fib(0)
    li   a1, 1           # fib(1)
loop:
    beqz t0, done
    add  t1, a0, a1
    mv   a0, a1
    mv   a1, t1
    addi t0, t0, -1
    j    loop
done:
    li   a7, 1
    ecall
    li   a7, 10
    ecall
