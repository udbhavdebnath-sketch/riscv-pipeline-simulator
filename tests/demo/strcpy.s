# Byte-wise string copy and length, with lb/lbu/sb: prints 13 then the
# sum of the copied bytes (signed lb): "Hello, RISC-V" -> 13, 1012
.text
.globl main
main:
    la   s0, src
    la   s1, dst
    li   a0, 0           # length
copy:
    lbu  t0, 0(s0)
    sb   t0, 0(s1)
    beqz t0, copied
    addi s0, s0, 1
    addi s1, s1, 1
    addi a0, a0, 1
    j    copy
copied:
    li   a7, 1
    ecall
    la   s1, dst
    li   a0, 0
sum:
    lb   t0, 0(s1)
    beqz t0, out
    add  a0, a0, t0
    addi s1, s1, 1
    j    sum
out:
    li   a7, 1
    ecall
    li   a7, 10
    ecall
.data
src: .byte 72, 101, 108, 108, 111, 44, 32, 82, 73, 83, 67, 45, 86, 0
.align 4
dst: .space 16
