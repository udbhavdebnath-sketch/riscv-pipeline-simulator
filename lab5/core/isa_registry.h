#ifndef ISA_REGISTRY_H
#define ISA_REGISTRY_H

/*
 * This is the single most important file in the base system.
 *
 * The simulator does NOT hardcode a big switch/if-else ladder over opcodes.
 * Instead, every instruction (base RV32I, and later the M/F/custom
 * extensions students add in later labs) is a struct registered into a
 * shared table. The decoder looks instructions up in this table by their
 * bit pattern; the assembler looks them up by mnemonic. Both therefore
 * share ONE source of truth about what each instruction is.
 *
 * Extending the ISA in a later lab means writing new InstructionHandler
 * entries and calling register_instruction() -- nothing in core/ ever
 * needs to change.
 */

#include <stdint.h>
#include "cpu_state.h"
#include "memory.h"

typedef enum {
    FMT_R,        /* rd, rs1, rs2                  e.g. add  a0, a1, a2      */
    FMT_I,        /* rd, rs1, imm                   e.g. addi a0, a1, 4       */
    FMT_I_LOAD,   /* rd, imm(rs1)                   e.g. lw   a0, 0(sp)       */
    FMT_I_JALR,   /* rd, imm(rs1)                   e.g. jalr ra, 0(t0)       */
    FMT_I_SHAMT,  /* rd, rs1, shamt (uses funct7)    e.g. slli a0, a1, 3       */
    FMT_I_SYS,    /* no operands                     e.g. ecall                */
    FMT_S,        /* rs2, imm(rs1)                   e.g. sw   a0, 0(sp)       */
    FMT_B,        /* rs1, rs2, label                 e.g. beq  a0, a1, foo     */
    FMT_U,        /* rd, imm                         e.g. lui  a0, 0x10000     */
    FMT_J,        /* rd, label                       e.g. jal  ra, foo        */
    FMT_R2,       /* rd, rs1 (rs2 field fixed)       e.g. fsqrt.s fa0, fa1    */
    FMT_R4,       /* rd, rs1, rs2, rs3               e.g. fmadd.s f0,f1,f2,f3 */
    FMT_CSR       /* rd, csr, rs1|zimm               e.g. csrrw t0, fcsr, t1  */
} InstrFormat;

/* Operand metadata, kept beside the encoding rather than in it. Which
 * register file each operand names, and how funct3 / rs2 are used. */
#define IF_RD_FP     0x01   /* rd is an f register                       */
#define IF_RS1_FP    0x02   /* rs1 is an f register                      */
#define IF_RS2_FP    0x04   /* rs2 is an f register                      */
#define IF_RS3_FP    0x08   /* rs3 is an f register (FMT_R4)             */
#define IF_HAS_RM    0x10   /* funct3 is a rounding mode, not an opcode  */
#define IF_RS2_FIXED 0x20   /* rs2 field is part of the opcode (rs2_sel) */

typedef struct {
    uint32_t opcode;
    uint32_t funct3;
    uint32_t funct7;
    uint32_t rd, rs1, rs2, rs3;
    int32_t  imm;           /* FMT_CSR: the 12-bit CSR number */
    uint32_t raw;
} DecodedInstr;

typedef void (*ExecuteFn)(CPUState *cpu, Memory *mem, DecodedInstr *instr);

typedef struct {
    const char *mnemonic;
    uint32_t    opcode;
    int32_t     funct3;   /* -1 means "don't care / not part of this encoding" */
    int32_t     funct7;   /* -1 means "don't care / not part of this encoding" */
    InstrFormat format;
    ExecuteFn   execute;
    uint32_t    flags;    /* IF_* operand metadata; 0 for every base instruction */
    int32_t     rs2_sel;  /* required rs2 field when flags has IF_RS2_FIXED      */
} InstructionHandler;

#define ISA_REGISTRY_MAX 256

void isa_registry_init(void);

/* Returns 0 on success, -1 if the table is full. Later registrations with
 * the same mnemonic are allowed (a lab overriding a base handler is a valid,
 * if unusual, use case) -- decode lookups return the FIRST opcode match and
 * mnemonic lookups return the LAST mnemonic match, so a later registration
 * for the same mnemonic "wins" for the assembler. */
int register_instruction(InstructionHandler handler);

/* register_instruction plus operand metadata (IF_* flags, fixed rs2). */
int register_instruction_ex(InstructionHandler handler, uint32_t flags, int32_t rs2_sel);

const InstructionHandler *isa_find_by_opcode(uint32_t opcode, uint32_t funct3, uint32_t funct7);
/* As above, also matching the rs2 field of IF_RS2_FIXED instructions. */
const InstructionHandler *isa_find_by_fields(uint32_t opcode, uint32_t funct3, uint32_t funct7, uint32_t rs2);
const InstructionHandler *isa_find_by_mnemonic(const char *mnemonic);

int isa_registry_count(void);
const InstructionHandler *isa_registry_get(int index);

#endif /* ISA_REGISTRY_H */
