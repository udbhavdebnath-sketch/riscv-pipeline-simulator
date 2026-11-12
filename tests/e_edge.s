# load-use, store-after-load, branch on loaded value, jal/jalr/ret, x0 writes, sb/sh/lh/lhu
.text
main:
    li   s0, buf
    addi t0, zero, -300
    sh   t0, 0(s0)
    lh   t1, 0(s0)
    lhu  t2, 0(s0)
    sw   t1, 4(s0)
    lw   t3, 4(s0)
    beq  t3, t1, ok1
    addi a1, zero, 99
ok1:
    addi zero, zero, 5
    add  zero, t1, t2
    jal  ra, func
    addi s1, a0, 1
    la_skip:
    lui  t4, 0xFFFFF
    srai t5, t4, 4
    srli t6, t4, 4
    sltu a2, zero, t4
    slt  a3, t4, zero
    auipc a4, 1
    sb   t4, 8(s0)
    lbu  a5, 8(s0)
    lb   a6, 9(s0)
    bge  a3, a2, skip
    addi s2, zero, 7
skip:
    blt  t4, zero, neg
    addi s3, zero, 1
neg:
    bltu t4, zero, never
    bgeu t4, zero, done
never:
    addi s4, zero, 1
done:
    mv   a0, s1
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
func:
    addi a0, zero, 41
    sub  a0, a0, zero
    xor  s5, a0, t1
    or   s6, s5, a0
    and  s7, s6, t2
    sll  s8, a0, a0
    sra  s9, t1, a0
    srl  s10, t1, a0
    slli s11, a0, 3
    xori s11, s11, -1
    ori  s11, s11, 1
    andi s11, s11, 0x7f
    slti gp, a0, 50
    sltiu tp, a0, -1
    ret
.data
buf: .word 0, 0, 0
