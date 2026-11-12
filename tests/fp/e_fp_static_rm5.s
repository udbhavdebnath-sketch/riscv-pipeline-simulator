# funct3 = 5 is a reserved rounding mode: build it by hand.
.text
main:
    li    t0, 3
    fcvt.s.w f1, t0
    .word 0x0010d153            # fadd.s f2, f1, f1 with funct3 = 5
    addi  t2, zero, 9
