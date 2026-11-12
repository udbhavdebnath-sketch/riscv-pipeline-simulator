# Data and control hazards on the 5-stage pipeline.
# Reconstructed for Lab 2 (Appendix A was missing from the handout).
# Expected: prints 30; 18 instructions; pipeline 44 cycles, 19 stalls, 3 flushes.

.text
.globl main
main:
    li   s0, buf            # lui+addi: RAW inside the pseudo-op
    addi t0, zero, 10       # t0 = 10
    addi t1, zero, 20       # t1 = 20   (independent)
    add  t2, t0, t1         # RAW on t1 (distance 1) and t0 (distance 2)
    sw   t2, 0(s0)          # RAW on t2 -- store data hazard
    lw   t3, 0(s0)          # reload 30
    add  t4, t3, zero       # load-use hazard
    beq  t4, t2, equal      # RAW on t4, taken -> flush
    addi t5, zero, -1       # skipped
equal:
    jal  ra, show           # jump -> flush
    addi a7, zero, 10       # exit
    ecall

show:
    slli a0, t4, 1          # a0 = 60
    srli a0, a0, 1          # RAW on a0 -> 30
    addi a7, zero, 1        # print a0
    ecall
    add  a1, a0, a0         # RAW on a0 at distance 3 -> 1 stall; a1 = 60
    jalr zero, 0(ra)        # return -> flush

.data
buf:
    .word 0
