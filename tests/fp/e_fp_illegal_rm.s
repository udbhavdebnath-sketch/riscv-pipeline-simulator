# frm = 5 is reserved: a dyn-mode instruction must fault as ILLEGAL without
# writing rd or any flag; everything older has retired.
.text
main:
    li    t0, 2
    fcvt.s.w f1, t0
    fsflags zero
    fsrmi 5
    addi  t1, zero, 9
    fadd.s f2, f1, f1           # dyn with frm=5 -> illegal
    addi  t2, zero, 9           # must not execute
