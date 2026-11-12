# Bubble sort of 10 words, then prints sum(i * a[i]) for i = 1..10 over the
# sorted array: 1*-7 + 2*0 + 3*3 + 4*5 + 5*8 + 6*12 + 7*19 + 8*21 + 9*44 + 10*90 = 1731
.text
.globl main
main:
    la   s0, arr
    li   s1, 10
outer:
    li   t0, 0           # swapped
    li   t1, 0           # i
    addi t2, s1, -1
inner:
    bge  t1, t2, endpass
    slli t3, t1, 2
    add  t3, s0, t3
    lw   t4, 0(t3)
    lw   t5, 4(t3)
    ble  t4, t5, noswap
    sw   t5, 0(t3)
    sw   t4, 4(t3)
    li   t0, 1
noswap:
    addi t1, t1, 1
    j    inner
endpass:
    bnez t0, outer
    li   a0, 0           # checksum
    li   t1, 0
sum:
    slli t3, t1, 2
    add  t3, s0, t3
    lw   t4, 0(t3)
    addi t1, t1, 1
    mv   t5, t1
mul:                     # a0 += t4 * t1 (repeated addition)
    beqz t5, next
    add  a0, a0, t4
    addi t5, t5, -1
    j    mul
next:
    blt  t1, s1, sum
    li   a7, 1
    ecall
    li   a7, 10
    ecall
.data
arr: .word 44, -7, 19, 3, 90, 0, 12, 5, 21, 8
