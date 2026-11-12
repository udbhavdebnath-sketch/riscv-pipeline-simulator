#include "rv32m.h"
#include "isa_registry.h"
#include <stdint.h>

/* Eight instructions added through the registry alone: no decoder,
 * assembler or pipeline change. Division never traps in RISC-V:
 *   x / 0        -> quotient all ones,  remainder x
 *   INT_MIN / -1 -> quotient INT_MIN,   remainder 0 */

#define OP_R      0x33
#define FUNCT7_M  0x01

static void exec_mul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) * cpu_read_reg(c, i->rs2));
}
static void exec_mulh(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int64_t p = (int64_t)(int32_t)cpu_read_reg(c, i->rs1) * (int64_t)(int32_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)((uint64_t)p >> 32));
}
static void exec_mulhsu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int64_t p = (int64_t)(int32_t)cpu_read_reg(c, i->rs1) * (int64_t)(uint64_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)((uint64_t)p >> 32));
}
static void exec_mulhu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint64_t p = (uint64_t)cpu_read_reg(c, i->rs1) * (uint64_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)(p >> 32));
}
static void exec_div(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1), b = (int32_t)cpu_read_reg(c, i->rs2);
    uint32_t q;
    if (b == 0)                        q = 0xFFFFFFFFu;
    else if (a == INT32_MIN && b == -1) q = (uint32_t)INT32_MIN;
    else                               q = (uint32_t)(a / b);
    cpu_write_reg(c, i->rd, q);
}
static void exec_divu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1), b = cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, b ? a / b : 0xFFFFFFFFu);
}
static void exec_rem(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1), b = (int32_t)cpu_read_reg(c, i->rs2);
    uint32_t r;
    if (b == 0)                        r = (uint32_t)a;
    else if (a == INT32_MIN && b == -1) r = 0;
    else                               r = (uint32_t)(a % b);
    cpu_write_reg(c, i->rd, r);
}
static void exec_remu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1), b = cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, b ? a % b : a);
}

void register_rv32m(void) {
    register_instruction((InstructionHandler){"mul",    OP_R, 0x0, FUNCT7_M, FMT_R, exec_mul});
    register_instruction((InstructionHandler){"mulh",   OP_R, 0x1, FUNCT7_M, FMT_R, exec_mulh});
    register_instruction((InstructionHandler){"mulhsu", OP_R, 0x2, FUNCT7_M, FMT_R, exec_mulhsu});
    register_instruction((InstructionHandler){"mulhu",  OP_R, 0x3, FUNCT7_M, FMT_R, exec_mulhu});
    register_instruction((InstructionHandler){"div",    OP_R, 0x4, FUNCT7_M, FMT_R, exec_div});
    register_instruction((InstructionHandler){"divu",   OP_R, 0x5, FUNCT7_M, FMT_R, exec_divu});
    register_instruction((InstructionHandler){"rem",    OP_R, 0x6, FUNCT7_M, FMT_R, exec_rem});
    register_instruction((InstructionHandler){"remu",   OP_R, 0x7, FUNCT7_M, FMT_R, exec_remu});
}
