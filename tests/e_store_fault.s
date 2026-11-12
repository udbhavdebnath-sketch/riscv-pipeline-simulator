# An out-of-bounds store must stop the machine precisely: older writes are
# visible, nothing younger commits.
.text
main:
    addi  t0, zero, 7
    lui   t1, 0x80000
    sw    t0, 0(t1)             # fault here
    addi  t2, zero, 9           # must not commit
    addi  t3, zero, 9
