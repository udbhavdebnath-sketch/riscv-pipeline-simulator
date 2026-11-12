#ifndef FP_HELPERS_H
#define FP_HELPERS_H

#include <stdint.h>
#include "cpu_state.h"
#include "isa_registry.h"

#define SIGN_BIT      0x80000000u
#define CANONICAL_NAN 0x7fc00000u

/* RISC-V rounding modes (funct3 / fcsr.frm). 5 and 6 are reserved. */
#define RM_RNE 0   /* to nearest, ties to even      */
#define RM_RTZ 1   /* toward zero                   */
#define RM_RDN 2   /* down, toward -inf             */
#define RM_RUP 3   /* up, toward +inf               */
#define RM_RMM 4   /* to nearest, ties away from 0  */
#define RM_DYN 7   /* use fcsr.frm                  */

typedef enum { AR_ADD, AR_SUB, AR_MUL, AR_DIV, AR_SQRT, AR_FMA } ArithOp;

/* uint32_t <-> float through memcpy: a pointer cast is a strict-aliasing bug. */
float    f_of(uint32_t b);
uint32_t bits_of(float f);

int is_nan_bits(uint32_t b);
int is_snan_bits(uint32_t b);

/* The instruction's funct3, or fcsr.frm when it is dyn. Returns -1 and
 * raises ILLEGAL_INSTRUCTION for modes 5, 6, and dyn with frm 5..7. */
int resolve_rm(CPUState *c, const DecodedInstr *i);

/* RISC-V mode (0..3) -> <fenv.h> constant for fesetround. */
int host_mode(int rm);

/* Host fetestexcept() bits -> FFLAG_* bits. */
uint32_t fflags_of(int host_excepts);

/* Rounds a double to float in mode rm (including RMM), ORing flags. */
float d2f(double d, int rm, uint32_t *flags);

/* op(A, B, C) on bit patterns in mode rm, with a single rounding. Returns
 * the result bits (any NaN canonicalised) and ORs exception flags in. */
uint32_t fp_arith(ArithOp op, uint32_t A, uint32_t B, uint32_t C, int rm, uint32_t *flags);

#endif /* FP_HELPERS_H */
