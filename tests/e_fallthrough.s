# no ecall: falls into .data -> illegal instruction, same pc as oracle
.text
main:
    addi t0, zero, 1
    addi t1, t0, 2
.data
    .word 0x12345678
