#include "fp_helpers.h"
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

float f_of(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }
uint32_t bits_of(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

int is_nan_bits(uint32_t b)  { return (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu) != 0; }
int is_snan_bits(uint32_t b) { return is_nan_bits(b) && !(b & 0x00400000u); }

int resolve_rm(CPUState *c, const DecodedInstr *i) {
    int rm = (int)i->funct3;
    if (rm == RM_DYN) rm = cpu_frm(c);
    if (rm <= RM_RMM) return rm;
    c->status = CPU_ILLEGAL_INSTRUCTION;
    c->fault_addr = c->pc;
    snprintf(c->fault_msg, sizeof(c->fault_msg),
             "invalid rounding mode %d (funct3=%u, frm=%d) at pc=0x%08x",
             rm, i->funct3, cpu_frm(c), c->pc);
    return -1;
}

int host_mode(int rm) {
    switch (rm) {
        case RM_RTZ: return FE_TOWARDZERO;
        case RM_RDN: return FE_DOWNWARD;
        case RM_RUP: return FE_UPWARD;
        default:     return FE_TONEAREST;   /* RNE, and RMM's starting point */
    }
}

uint32_t fflags_of(int ex) {
    uint32_t f = 0;
    if (ex & FE_INVALID)   f |= FFLAG_NV;
    if (ex & FE_DIVBYZERO) f |= FFLAG_DZ;
    if (ex & FE_OVERFLOW)  f |= FFLAG_OF | FFLAG_NX;
    if (ex & FE_UNDERFLOW) f |= FFLAG_UF | FFLAG_NX;  /* host only raises UF when inexact */
    if (ex & FE_INEXACT)   f |= FFLAG_NX;
    return f;
}

/* `exact` is the true result if it fits a double (which it always does when
 * it is a float tie: a tie has at most 25 significant bits), and `r` is the
 * round-to-nearest-even float. Fix r up to ties-away-from-zero. */
static float rmm_fixup(double exact, float r, uint32_t *flags) {
    if ((double)r == exact || isnan(r) || isinf(r)) return r;
    float lo = r, hi = r;
    if ((double)r < exact) hi = nextafterf(r, INFINITY); else lo = nextafterf(r, -INFINITY);
    if ((double)hi - exact != exact - (double)lo) return r;     /* not a tie */
    float away = (exact > 0) ? hi : lo;
    if (isinf(away)) *flags |= FFLAG_OF | FFLAG_NX;
    return away;
}

float d2f(double d, int rm, uint32_t *flags) {
    fesetround(host_mode(rm));
    feclearexcept(FE_ALL_EXCEPT);
    volatile double vd = d;
    volatile float r = (float)vd;
    *flags |= fflags_of(fetestexcept(FE_ALL_EXCEPT));
    fesetround(FE_TONEAREST);
    return (rm == RM_RMM) ? rmm_fixup(d, r, flags) : r;
}

/* The exact result in double, or NAN if a double cannot hold it exactly
 * (then it cannot be a float tie either). Only add/sub/mul/fma can tie. */
static double exact_in_double(ArithOp op, float a, float b, float c) {
    feclearexcept(FE_ALL_EXCEPT);
    volatile double x = a, y = b, z = c, r;
    switch (op) {
        case AR_ADD: r = x + y; break;
        case AR_SUB: r = x - y; break;
        case AR_MUL: r = x * y; break;
        case AR_FMA: r = fma(x, y, z); break;
        default:     return NAN;
    }
    return fetestexcept(FE_INEXACT) ? NAN : r;
}

uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags) {
    volatile float a = f_of(A), b = f_of(B), c = f_of(C), r;

    /* RISC-V rule (IEEE leaves it open): inf x 0 is invalid even when the
     * addend is a quiet NaN. */
    if (op == AR_FMA && ((isinf(a) && b == 0.0f) || (a == 0.0f && isinf(b)))) {
        *flags |= FFLAG_NV;
        return CANONICAL_NAN;
    }

    fesetround(host_mode(rm));
    feclearexcept(FE_ALL_EXCEPT);
    switch (op) {
        case AR_ADD:  r = a + b;          break;
        case AR_SUB:  r = a - b;          break;
        case AR_MUL:  r = a * b;          break;
        case AR_DIV:  r = a / b;          break;
        case AR_SQRT: r = sqrtf(a);       break;
        case AR_FMA:  r = fmaf(a, b, c);  break;
        default:      r = 0.0f;           break;
    }
    uint32_t got = fflags_of(fetestexcept(FE_ALL_EXCEPT));
    fesetround(FE_TONEAREST);

    float res = r;
    if (rm == RM_RMM && (got & FFLAG_NX)) {
        double exact = exact_in_double(op, a, b, c);
        if (!isnan(exact)) res = rmm_fixup(exact, res, &got);
    }
    *flags |= got;
    return isnan(res) ? CANONICAL_NAN : bits_of(res);
}
