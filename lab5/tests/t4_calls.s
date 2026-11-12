# Calls and returns: every jal is free once IF pre-decodes it, every
# return (jalr) costs 2 cycles because nothing predicts its target.
# A walk over a 9-entry table: for each entry, sum() calls scale(), which
# loads the entry and adds it in (doubling the special entry 4).
# Expected: prints 50. 219 instructions, 20 branches, 33 jal, 18 jalr,
# 15 load-use stalls under forwarding.
.text
.globl main
main:
    li   sp, stack_top
    li   s3, params
    lw   s1, 0(s3)          # entries = 9
    blt  s1, zero, fail     # C: sanity check, never taken (load-use)
    lw   s2, 4(s3)          # special entry = 4
    add  s4, s2, s2         # load-use
    lw   t0, 8(s3)          # table base
    add  s5, t0, zero       # load-use
    lw   t0, 12(s3)
    add  a2, t0, zero       # load-use: count = 0
    lw   t0, 16(s3)
    add  a3, t0, zero       # load-use: index sum = 0
    lw   t0, 20(s3)
    add  a4, t0, zero       # load-use: checksum = 0
    addi a5, zero, 0        # or-accumulator
    j    go1                # jal chain: straight-line jumps
go1: j   go2
go2: j   go3
go3: j   go4
go4: j   go5
go5: j   loop
loop:
    beq  s0, s1, done       # A: N x9, then T (forward exit)
    jal  ra, sum
    addi s0, s0, 1
    j    loop
done:
    mv   a0, a1
    addi a7, zero, 1
    ecall
fail:
    addi a7, zero, 10
    ecall

sum:                        # a1 += table[s0] (x2 for the special entry)
    addi sp, sp, -4
    sw   ra, 0(sp)
    slli t0, s0, 2
    add  a0, s5, t0
    jal  ra, scale
    addi a2, a2, 1          # entries visited
    add  a3, a3, s0         # index sum
    xor  a4, a4, t1         # checksum of the values
    or   a5, a5, t1
    sub  a6, a1, a3         # running total minus index sum
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

scale:
    lw   t1, 0(a0)
    add  a1, a1, t1         # load-use: one stall
    beq  s0, s2, twice      # B: N N N N T N N N N (forward)
    ret
twice:
    add  a1, a1, t1
    ret

.data
params:
    .word 9, 4, table, 0, 0, 0
table:
    .word 1, 2, 3, 4, 5, 6, 7, 8, 9
    .word 0, 0, 0, 0, 0, 0, 0
stack_top:
    .word 0
