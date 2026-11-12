#include "zicsr.h"
#include "isa_registry.h"
#include <stdio.h>

#define OP_SYSTEM 0x73

static int csr_read(CPUState *c, uint32_t csr, uint32_t *out) {
    switch (csr) {
        case CSR_FFLAGS: *out = c->fcsr & 0x1F;        return 1;
        case CSR_FRM:    *out = (c->fcsr >> 5) & 0x7;  return 1;
        case CSR_FCSR:   *out = c->fcsr & 0xFF;        return 1;
    }
    return 0;
}

static void csr_write(CPUState *c, uint32_t csr, uint32_t v) {
    switch (csr) {
        case CSR_FFLAGS: c->fcsr = (c->fcsr & ~0x1Fu) | (v & 0x1F);        break;
        case CSR_FRM:    c->fcsr = (c->fcsr & ~0xE0u) | ((v & 0x7) << 5);  break;
        case CSR_FCSR:   c->fcsr = v & 0xFF;                               break;
    }
}

/* op: 1 = write, 2 = set bits, 3 = clear bits. src is x[rs1] or the
 * 5-bit immediate. Set/clear with a zero source register/immediate does
 * not write, as the spec requires. */
static void do_csr(CPUState *c, DecodedInstr *i, int op, uint32_t src, int writes) {
    uint32_t csr = (uint32_t)i->imm, old;
    if (!csr_read(c, csr, &old)) {
        c->status = CPU_ILLEGAL_INSTRUCTION;
        c->fault_addr = c->pc;
        snprintf(c->fault_msg, sizeof(c->fault_msg), "unknown CSR 0x%03x at pc=0x%08x", csr, c->pc);
        return;
    }
    if (writes) {
        uint32_t v = (op == 1) ? src : (op == 2) ? (old | src) : (old & ~src);
        csr_write(c, csr, v);
    }
    cpu_write_reg(c, i->rd, old);
}

static void exec_csrrw(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 1, cpu_read_reg(c, i->rs1), 1);
}
static void exec_csrrs(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 2, cpu_read_reg(c, i->rs1), i->rs1 != 0);
}
static void exec_csrrc(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 3, cpu_read_reg(c, i->rs1), i->rs1 != 0);
}
static void exec_csrrwi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 1, i->rs1, 1);
}
static void exec_csrrsi(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 2, i->rs1, i->rs1 != 0);
}
static void exec_csrrci(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    do_csr(c, i, 3, i->rs1, i->rs1 != 0);
}

void register_zicsr(void) {
    register_instruction((InstructionHandler){"csrrw",  OP_SYSTEM, 0x1, -1, FMT_CSR, exec_csrrw});
    register_instruction((InstructionHandler){"csrrs",  OP_SYSTEM, 0x2, -1, FMT_CSR, exec_csrrs});
    register_instruction((InstructionHandler){"csrrc",  OP_SYSTEM, 0x3, -1, FMT_CSR, exec_csrrc});
    register_instruction((InstructionHandler){"csrrwi", OP_SYSTEM, 0x5, -1, FMT_CSR, exec_csrrwi});
    register_instruction((InstructionHandler){"csrrsi", OP_SYSTEM, 0x6, -1, FMT_CSR, exec_csrrsi});
    register_instruction((InstructionHandler){"csrrci", OP_SYSTEM, 0x7, -1, FMT_CSR, exec_csrrci});
}
