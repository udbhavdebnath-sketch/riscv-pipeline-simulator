#include "rv32f.h"
#include "fp_helpers.h"
#include "isa_registry.h"
#include <math.h>
#include <stdio.h>

#define OP_LOAD_FP  0x07
#define OP_STORE_FP 0x27
#define OP_FP       0x53
#define OP_FMADD    0x43
#define OP_FMSUB    0x47
#define OP_FNMSUB   0x4B
#define OP_FNMADD   0x4F

#define FREG(r) cpu_read_freg(c, (int)(r))

static void reg(const char *mn, uint32_t op, int32_t f3, int32_t f7, InstrFormat fmt,
                ExecuteFn fn, uint32_t flags, int32_t rs2_sel) {
    register_instruction_ex((InstructionHandler){mn, op, f3, f7, fmt, fn}, flags, rs2_sel);
}

/* =======================  Task 1: bits only  ======================= */

static void fp_mem_fault(CPUState *c, uint32_t addr, const char *what) {
    c->status = CPU_MEM_FAULT;
    c->fault_addr = addr;
    snprintf(c->fault_msg, sizeof(c->fault_msg), "%s out of bounds at address 0x%08x", what, addr);
}

static void exec_flw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    uint32_t v;
    if (memory_read_word(m, addr, &v) != MEM_OK) { fp_mem_fault(c, addr, "flw"); return; }
    cpu_write_freg(c, i->rd, v);
}
static void exec_fsw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, i->rs1) + (uint32_t)i->imm;
    if (memory_write_word(m, addr, FREG(i->rs2)) != MEM_OK) fp_mem_fault(c, addr, "fsw");
}

/* Sign injection: pure bit operations, so NaN payloads survive and no
 * flag is ever raised. fmv.s / fneg.s / fabs.s are these with rs1 == rs2. */
static void exec_fsgnj(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjn(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (~FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjx(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, i->rd, FREG(i->rs1) ^ (FREG(i->rs2) & SIGN_BIT));
}

/* Moves reinterpret bits; they never convert. */
static void exec_fmv_x_w(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, FREG(i->rs1));
}
static void exec_fmv_w_x(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, i->rd, cpu_read_reg(c, i->rs1));
}

/* One-hot: -inf -normal -subnormal -0 +0 +subnormal +normal +inf sNaN qNaN */
static void exec_fclass(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t b    = FREG(i->rs1);
    int      neg  = (b & SIGN_BIT) != 0;
    uint32_t exp  = (b >> 23) & 0xFF;
    uint32_t frac = b & 0x007FFFFFu;
    int bit;
    if (exp == 0xFF)     bit = frac ? ((frac & 0x00400000u) ? 9 : 8) : (neg ? 0 : 7);
    else if (exp == 0)   bit = frac ? (neg ? 2 : 5) : (neg ? 3 : 4);
    else                 bit = neg ? 1 : 6;
    cpu_write_reg(c, i->rd, 1u << bit);
}

/* =======================  Task 2: arithmetic  ======================= */

/* Resolve the mode first: an illegal instruction must not write a
 * register or a flag. */
static void do_arith(CPUState *c, DecodedInstr *i, ArithOp op, uint32_t A, uint32_t B, uint32_t C) {
    int rm = resolve_rm(c, i);
    if (rm < 0) return;
    uint32_t flags = 0;
    uint32_t r = fp_arith(op, A, B, C, rm, &flags);
    cpu_raise_fflags(c, flags);
    cpu_write_freg(c, i->rd, r);
}

static void exec_fadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_ADD, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_SUB, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fmul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_MUL, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fdiv(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_DIV, FREG(i->rs1), FREG(i->rs2), 0);
}
static void exec_fsqrt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_SQRT, FREG(i->rs1), 0, 0);
}
/* One rounding at the end; the four differ only in sign flips, which are
 * bit operations on the operands. fnmsub adds and fnmadd subtracts. */
static void exec_fmadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1), FREG(i->rs2), FREG(i->rs3));
}
static void exec_fmsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1), FREG(i->rs2), FREG(i->rs3) ^ SIGN_BIT);
}
static void exec_fnmsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1) ^ SIGN_BIT, FREG(i->rs2), FREG(i->rs3));
}
static void exec_fnmadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_arith(c, i, AR_FMA, FREG(i->rs1) ^ SIGN_BIT, FREG(i->rs2), FREG(i->rs3) ^ SIGN_BIT);
}

/* =======================  Task 3: where RISC-V disagrees with C  ======================= */

/* feq.s is the quiet compare: NV only for a signalling NaN. */
static void exec_feq(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t A = FREG(i->rs1), B = FREG(i->rs2);
    if (is_snan_bits(A) || is_snan_bits(B)) cpu_raise_fflags(c, FFLAG_NV);
    int r = !is_nan_bits(A) && !is_nan_bits(B) && f_of(A) == f_of(B);
    cpu_write_reg(c, i->rd, (uint32_t)r);
}
/* flt.s / fle.s signal on any NaN. */
static void do_order(CPUState *c, DecodedInstr *i, int le) {
    uint32_t A = FREG(i->rs1), B = FREG(i->rs2);
    if (is_nan_bits(A) || is_nan_bits(B)) {
        cpu_raise_fflags(c, FFLAG_NV);
        cpu_write_reg(c, i->rd, 0);
        return;
    }
    int r = le ? (f_of(A) <= f_of(B)) : (f_of(A) < f_of(B));
    cpu_write_reg(c, i->rd, (uint32_t)r);
}
static void exec_flt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_order(c, i, 0); }
static void exec_fle(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_order(c, i, 1); }

/* A NaN never wins; two NaNs give the canonical NaN; -0 < +0. */
static void do_minmax(CPUState *c, DecodedInstr *i, int is_max) {
    uint32_t A = FREG(i->rs1), B = FREG(i->rs2), r;
    if (is_snan_bits(A) || is_snan_bits(B)) cpu_raise_fflags(c, FFLAG_NV);
    if (is_nan_bits(A) && is_nan_bits(B)) r = CANONICAL_NAN;
    else if (is_nan_bits(A))              r = B;
    else if (is_nan_bits(B))              r = A;
    else if (((A | B) & ~SIGN_BIT) == 0)  r = is_max ? (A & B) : (A | B);  /* two zeros */
    else {
        int a_less = f_of(A) < f_of(B);
        r = (a_less != is_max) ? A : B;
    }
    cpu_write_freg(c, i->rd, r);
}
static void exec_fmin(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_minmax(c, i, 0); }
static void exec_fmax(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_minmax(c, i, 1); }

/* Every float is exact in double, so round there without losing a flag. */
static double round_rm(double v, int rm) {
    switch (rm) {
        case RM_RTZ: return trunc(v);
        case RM_RDN: return floor(v);
        case RM_RUP: return ceil(v);
        case RM_RMM: return round(v);         /* half away from zero */
        default:     return nearbyint(v);     /* host mode is always RNE here */
    }
}

/* Float -> integer: NaN and out-of-range saturate with NV (and no NX);
 * an in-range result that had to be rounded raises NX. Never a C cast of
 * an out-of-range value. */
static void do_fcvt_to_int(CPUState *c, DecodedInstr *i, int is_unsigned) {
    int rm = resolve_rm(c, i);
    if (rm < 0) return;
    uint32_t A = FREG(i->rs1);
    double lo = is_unsigned ? 0.0 : -2147483648.0;
    double hi = is_unsigned ? 4294967295.0 : 2147483647.0;
    uint32_t max = is_unsigned ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    uint32_t min = is_unsigned ? 0u : 0x80000000u;
    uint32_t r;
    if (is_nan_bits(A)) {
        cpu_raise_fflags(c, FFLAG_NV);
        r = max;
    } else {
        double v = f_of(A), q = round_rm(v, rm);
        if (q > hi)      { cpu_raise_fflags(c, FFLAG_NV); r = max; }
        else if (q < lo) { cpu_raise_fflags(c, FFLAG_NV); r = min; }
        else {
            r = is_unsigned ? (uint32_t)q : (uint32_t)(int32_t)q;
            if (q != v) cpu_raise_fflags(c, FFLAG_NX);
        }
    }
    cpu_write_reg(c, i->rd, r);
}
static void exec_fcvt_w_s(CPUState *c, Memory *m, DecodedInstr *i)  { (void)m; do_fcvt_to_int(c, i, 0); }
static void exec_fcvt_wu_s(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; do_fcvt_to_int(c, i, 1); }

/* Integer -> float: exact in double, one rounding to float (NX above 24 bits). */
static void do_fcvt_from_int(CPUState *c, DecodedInstr *i, double v) {
    int rm = resolve_rm(c, i);
    if (rm < 0) return;
    uint32_t flags = 0;
    float r = d2f(v, rm, &flags);
    cpu_raise_fflags(c, flags);
    cpu_write_freg(c, i->rd, bits_of(r));
}
static void exec_fcvt_s_w(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_fcvt_from_int(c, i, (double)(int32_t)cpu_read_reg(c, i->rs1));
}
static void exec_fcvt_s_wu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_fcvt_from_int(c, i, (double)cpu_read_reg(c, i->rs1));
}

/* =======================  Registration  ======================= */

#define FFF  (IF_RD_FP | IF_RS1_FP | IF_RS2_FP)               /* fd, fs1, fs2       */
#define XFF  (IF_RS1_FP | IF_RS2_FP)                          /* rd, fs1, fs2       */
#define FFFF (IF_RD_FP | IF_RS1_FP | IF_RS2_FP | IF_RS3_FP)   /* fd, fs1, fs2, fs3  */
#define FIX  IF_RS2_FIXED

void register_rv32f(void) {
    /* Task 1 */
    reg("flw",      OP_LOAD_FP,  0x2, -1,   FMT_I_LOAD, exec_flw,     IF_RD_FP,  0);
    reg("fsw",      OP_STORE_FP, 0x2, -1,   FMT_S,      exec_fsw,     IF_RS2_FP, 0);
    reg("fsgnj.s",  OP_FP,       0x0, 0x10, FMT_R,      exec_fsgnj,   FFF,       0);
    reg("fsgnjn.s", OP_FP,       0x1, 0x10, FMT_R,      exec_fsgnjn,  FFF,       0);
    reg("fsgnjx.s", OP_FP,       0x2, 0x10, FMT_R,      exec_fsgnjx,  FFF,       0);
    reg("fmv.x.w",  OP_FP,       0x0, 0x70, FMT_R2,     exec_fmv_x_w, IF_RS1_FP | FIX, 0);
    reg("fclass.s", OP_FP,       0x1, 0x70, FMT_R2,     exec_fclass,  IF_RS1_FP | FIX, 0);
    reg("fmv.w.x",  OP_FP,       0x0, 0x78, FMT_R2,     exec_fmv_w_x, IF_RD_FP  | FIX, 0);

    /* Task 2: funct3 holds the rounding mode, so it is don't-care */
    reg("fadd.s",   OP_FP,     -1, 0x00, FMT_R,  exec_fadd,   FFF  | IF_HAS_RM, 0);
    reg("fsub.s",   OP_FP,     -1, 0x04, FMT_R,  exec_fsub,   FFF  | IF_HAS_RM, 0);
    reg("fmul.s",   OP_FP,     -1, 0x08, FMT_R,  exec_fmul,   FFF  | IF_HAS_RM, 0);
    reg("fdiv.s",   OP_FP,     -1, 0x0C, FMT_R,  exec_fdiv,   FFF  | IF_HAS_RM, 0);
    reg("fsqrt.s",  OP_FP,     -1, 0x2C, FMT_R2, exec_fsqrt,  IF_RD_FP | IF_RS1_FP | IF_HAS_RM | FIX, 0);
    /* funct7 = rs3 | fmt: don't-care too */
    reg("fmadd.s",  OP_FMADD,  -1, -1,   FMT_R4, exec_fmadd,  FFFF | IF_HAS_RM, 0);
    reg("fmsub.s",  OP_FMSUB,  -1, -1,   FMT_R4, exec_fmsub,  FFFF | IF_HAS_RM, 0);
    reg("fnmsub.s", OP_FNMSUB, -1, -1,   FMT_R4, exec_fnmsub, FFFF | IF_HAS_RM, 0);
    reg("fnmadd.s", OP_FNMADD, -1, -1,   FMT_R4, exec_fnmadd, FFFF | IF_HAS_RM, 0);

    /* Task 3 */
    reg("feq.s",     OP_FP, 0x2, 0x50, FMT_R,  exec_feq,       XFF, 0);
    reg("flt.s",     OP_FP, 0x1, 0x50, FMT_R,  exec_flt,       XFF, 0);
    reg("fle.s",     OP_FP, 0x0, 0x50, FMT_R,  exec_fle,       XFF, 0);
    reg("fmin.s",    OP_FP, 0x0, 0x14, FMT_R,  exec_fmin,      FFF, 0);
    reg("fmax.s",    OP_FP, 0x1, 0x14, FMT_R,  exec_fmax,      FFF, 0);
    reg("fcvt.w.s",  OP_FP, -1,  0x60, FMT_R2, exec_fcvt_w_s,  IF_RS1_FP | IF_HAS_RM | FIX, 0);
    reg("fcvt.wu.s", OP_FP, -1,  0x60, FMT_R2, exec_fcvt_wu_s, IF_RS1_FP | IF_HAS_RM | FIX, 1);
    reg("fcvt.s.w",  OP_FP, -1,  0x68, FMT_R2, exec_fcvt_s_w,  IF_RD_FP  | IF_HAS_RM | FIX, 0);
    reg("fcvt.s.wu", OP_FP, -1,  0x68, FMT_R2, exec_fcvt_s_wu, IF_RD_FP  | IF_HAS_RM | FIX, 1);
}
