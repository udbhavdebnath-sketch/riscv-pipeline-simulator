#ifndef CONTROL_H
#define CONTROL_H

#include "isa_registry.h"

typedef enum { WB_NONE = 0, WB_ALU, WB_MEM, WB_PC4 } WbSrc;

typedef struct {
    int   reg_write;    /* does this instruction write rd?         */
    WbSrc wb_src;       /* what value gets written back?           */
    int   mem_read;     /* MEM stage performs a load               */
    int   mem_write;    /* MEM stage performs a store              */
    int   is_branch;    /* conditional branch - EX decides         */
    int   is_jump;      /* jal / jalr - EX always redirects        */
    int   is_system;    /* ecall - handled at WB                   */
    int   uses_rs1;     /* needed by the hazard detector           */
    int   uses_rs2;
    int   uses_rs3;     /* fused multiply-add third source         */
    /* Unified register numbers: x1..x31 -> 1..31, f0..f31 -> 32..63,
     * x0 / unused -> REG_NONE. The hazard unit compares only these, so it
     * never needs to know which file a register lives in. */
    int   src1, src2, src3, dst;
    int   exec_in_ex;   /* extension instruction: run its handler in EX */
} ControlSignals;

#define REG_NONE 0
#define REG_F0   32

ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h);

#endif /* CONTROL_H */
