# Control-flow corner cases. Expected: prints 7, then exits.
.text
.globl main
main:
    li    s1, 0
    j     hop1                  # jump straight onto another jump
hop1:
    j     hop2
hop2:
    addi  s1, s1, 1             # 1
    la    t0, odd
    addi  t0, t0, 1             # odd address: jalr must clear bit 0
    jalr  ra, 0(t0)
    addi  s1, s1, 1             # 3 (after return)
    jal   zero, skip            # jal with x0 as link
    addi  s1, s1, 100           # skipped
skip:
    beq   zero, zero, t1        # taken branch ...
t1: bne   s1, zero, t2          # ... immediately followed by another taken one
    addi  s1, s1, 100
t2: addi  s1, s1, 1             # 4
    la    t1, here
    jalr  t2, 0(t1)             # target is exactly pc+4
here:
    addi  s1, s1, 1             # 5
    addi  a7, zero, 99          # unknown syscall: warning, execution continues
    ecall
    addi  s1, s1, 2             # 7
    mv    a0, s1
    addi  a7, zero, 1
    ecall
    addi  a7, zero, 10
    ecall
odd:
    addi  s1, s1, 1             # 2
    ret
