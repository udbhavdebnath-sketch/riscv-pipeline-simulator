# GCD by repeated subtraction: prints gcd(1071, 462) = 21
.text
.globl main
main:
    li   a0, 1071
    li   a1, 462
loop:
    beq  a0, a1, done
    bgtu a0, a1, bigger
    sub  a1, a1, a0
    j    loop
bigger:
    sub  a0, a0, a1
    j    loop
done:
    li   a7, 1
    ecall
    li   a7, 10
    ecall
