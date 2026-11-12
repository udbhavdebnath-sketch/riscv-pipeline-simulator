# out-of-bounds load must fault precisely (earlier writes visible, later not)
.text
main:
    addi t0, zero, 7
    lui  t1, 0x80000
    lw   t2, 0(t1)
    addi t3, zero, 9
