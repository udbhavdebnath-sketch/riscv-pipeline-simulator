# An inner loop of 3 iterations, entered 4 times: the case where a 1-bit
# predictor mispredicts both the exit and the re-entry of every pass.
# Expected: a0 = 24. 52 instructions, 16 branches (inner 12, outer 4).
.text
.globl main
main:
    addi s0, zero, 4        # outer passes
    addi a0, zero, 0        # sum
outer:
    addi t0, zero, 3        # inner trip count
inner:
    add  a0, a0, t0         # sum += 3 + 2 + 1
    addi t0, t0, -1
    bne  t0, zero, inner    # T T N per pass
    addi s0, s0, -1
    bne  s0, zero, outer    # T T T N
    addi a7, zero, 10
    ecall
