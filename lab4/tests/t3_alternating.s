# A forward branch whose outcome alternates T N T N ...: no predictor that
# looks only at this branch's own history can follow it.
# Expected: prints 8 (4 odd + 4 even). 50 instructions, 16 branches, 4 jal.
.text
.globl main
main:
    addi t2, zero, 8        # iterations
loop:
    andi t1, t0, 1
    beq  t1, zero, even     # taken when t0 is even: T N T N T N T N
    addi s1, s1, 1          # odd count
    j    next
even:
    addi s2, s2, 1          # even count
next:
    addi t0, t0, 1
    blt  t0, t2, loop       # backward: T x7, N x1
    add  a0, s1, s2
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
