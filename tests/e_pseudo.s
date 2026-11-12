# Every RV32I pseudo-op the assemblers accept. Each check adds a distinct
# bit to s1 when the pseudo-op did the right thing.
# Expected: prints 1048575 (all 20 bits set), s1 = 0x000fffff.
.text
.globl main
main:
    la    s0, val               # la == li with a label
    lw    t0, 0(s0)             # 5
    li    t1, -3
    li    s1, 0

    beqz  zero, b0
    j     fail
b0: ori   s1, s1, 0x1
    bnez  t0, b1
    j     fail
b1: ori   s1, s1, 0x2
    blez  t1, b2
    j     fail
b2: ori   s1, s1, 0x4
    bgez  t0, b3
    j     fail
b3: ori   s1, s1, 0x8
    bltz  t1, b4
    j     fail
b4: ori   s1, s1, 0x10
    bgtz  t0, b5
    j     fail
b5: ori   s1, s1, 0x20
    bgt   t0, t1, b6            # 5 > -3
    j     fail
b6: ori   s1, s1, 0x40
    ble   t1, t0, b7            # -3 <= 5
    j     fail
b7: ori   s1, s1, 0x80
    bgtu  t1, t0, b8            # 0xfffffffd > 5 unsigned
    j     fail
b8: ori   s1, s1, 0x100
    bleu  t0, t1, b9
    j     fail
b9: ori   s1, s1, 0x200

    neg   t2, t0                # -5
    addi  t2, t2, 5
    seqz  t3, t2                # 1
    slli  t3, t3, 10
    or    s1, s1, t3            # 0x400
    not   t2, zero              # -1
    snez  t3, t2                # 1
    slli  t3, t3, 11
    or    s1, s1, t3            # 0x800
    sltz  t3, t1                # 1
    slli  t3, t3, 12
    or    s1, s1, t3            # 0x1000
    sgtz  t3, t0                # 1
    slli  t3, t3, 13
    or    s1, s1, t3            # 0x2000

    call  f1                    # 0x4000
    jal   f2                    # 0x8000 (one-operand jal links ra)
    la    t4, f3
    jalr  t4                    # 0x10000 (one-operand jalr links ra)
    la    t4, back
    la    ra, f4
    jr    ra                    # 0x20000
back:
    call  f5                    # f5 tail-calls f6: 0x40000 | 0x80000
    j     done
f1: lui   t5, 0x4
    or    s1, s1, t5
    ret
f2: lui   t5, 0x8
    or    s1, s1, t5
    ret
f3: lui   t5, 0x10
    or    s1, s1, t5
    ret
f4: lui   t5, 0x20
    or    s1, s1, t5
    jr    t4
f5: lui   t5, 0x40
    or    s1, s1, t5
    tail  f6
f6: lui   t5, 0x80
    or    s1, s1, t5
    ret

done:
    mv    a0, s1
    addi  a7, zero, 1
    ecall
fail:
    addi  a7, zero, 10
    ecall
.data
val: .word 5
