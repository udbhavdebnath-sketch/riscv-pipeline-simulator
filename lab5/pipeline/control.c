#include "control.h"
#include <string.h>

static int unify(uint32_t idx, int is_fp) {
    if (is_fp) return REG_F0 + (int)idx;
    return idx ? (int)idx : REG_NONE;
}

/* One switch on the registry's format: later stages act only on these
 * signals and never re-examine the opcode. Which file each operand names
 * comes from the registry's IF_* flags. */
ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h) {
    ControlSignals c;
    memset(&c, 0, sizeof(c));
    switch (h->format) {
        case FMT_R:
            c.reg_write = 1; c.wb_src = WB_ALU; c.uses_rs1 = 1; c.uses_rs2 = 1;
            break;
        case FMT_I:
        case FMT_I_SHAMT:
            c.reg_write = 1; c.wb_src = WB_ALU; c.uses_rs1 = 1;
            break;
        case FMT_U:
            c.reg_write = 1; c.wb_src = WB_ALU;
            break;
        case FMT_I_LOAD:
            c.reg_write = 1; c.wb_src = WB_MEM; c.mem_read = 1; c.uses_rs1 = 1;
            break;
        case FMT_S:
            c.mem_write = 1; c.uses_rs1 = 1; c.uses_rs2 = 1;
            break;
        case FMT_B:
            c.is_branch = 1; c.uses_rs1 = 1; c.uses_rs2 = 1;
            break;
        case FMT_J:
            c.reg_write = 1; c.wb_src = WB_PC4; c.is_jump = 1;
            break;
        case FMT_I_JALR:
            c.reg_write = 1; c.wb_src = WB_PC4; c.is_jump = 1; c.uses_rs1 = 1;
            break;
        case FMT_I_SYS:
            c.is_system = 1;
            break;
        case FMT_R2:
            c.reg_write = 1; c.wb_src = WB_ALU; c.uses_rs1 = 1;
            break;
        case FMT_R4:
            c.reg_write = 1; c.wb_src = WB_ALU; c.uses_rs1 = 1; c.uses_rs2 = 1; c.uses_rs3 = 1;
            break;
        case FMT_CSR:   /* funct3 5..7: the rs1 field is an immediate */
            c.reg_write = 1; c.wb_src = WB_ALU; c.uses_rs1 = (i->funct3 < 4);
            break;
    }

    uint32_t fl = h->flags;
    c.dst  = c.reg_write ? unify(i->rd,  fl & IF_RD_FP)  : REG_NONE;
    c.src1 = c.uses_rs1  ? unify(i->rs1, fl & IF_RS1_FP) : REG_NONE;
    c.src2 = c.uses_rs2  ? unify(i->rs2, fl & IF_RS2_FP) : REG_NONE;
    c.src3 = c.uses_rs3  ? unify(i->rs3, fl & IF_RS3_FP) : REG_NONE;
    /* ECALL consumes the syscall number and argument just like EX operands. */
    if (c.is_system) { c.src1 = 17; c.src2 = 10; }

    /* The pipeline's own ALU covers RV32I; everything the registry added
     * later (M, F, Zicsr) runs its handler in EX instead. */
    c.exec_in_ex = h->format == FMT_R2 || h->format == FMT_R4 || h->format == FMT_CSR
                || (h->format == FMT_R && (h->opcode != 0x33 || h->funct7 == 0x01));
    return c;
}
