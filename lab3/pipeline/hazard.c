#include "hazard.h"

int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs) {
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c) {
    HazardVerdict v = {0, 0};
    /* An unused source becomes x0, which produces() rejects: lui must not
     * "read" the junk in its rs1 field. */
    uint32_t s1 = c->uses_rs1 ? d->rs1 : 0;
    uint32_t s2 = c->uses_rs2 ? d->rs2 : 0;

    if (!p->forwarding) {                     /* Task 1: stall on any of 3 latches */
        if (produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s1) ||
            produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s2) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s1) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s2) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s1) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s2))
            v.stall = 1;
        return v;
    }

    /* Task 3: forwarding covers everything except a load still in EX. Its
     * value exists only at the end of MEM, when the consumer needs it at the
     * start of EX: one stall, then MEM/WB -> EX delivers it. */
    if (p->id_ex.valid && p->id_ex.ctrl.mem_read && p->id_ex.instr.rd != 0 &&
        ((c->uses_rs1 && d->rs1 == p->id_ex.instr.rd) ||
         (c->uses_rs2 && d->rs2 == p->id_ex.instr.rd))) {
        v.stall = 1;
        v.load_use = 1;
    }
    return v;
}

/* One mux per operand. EX/MEM holds the younger producer, so it wins. */
static uint32_t forward_one(const Pipeline *p, uint32_t rs, uint32_t latched, int *fwd_ex, int *fwd_mem) {
    if (produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, rs)) {
        (*fwd_ex)++;
        return p->ex_mem.alu_result;
    }
    if (produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, rs)) {
        (*fwd_mem)++;
        return p->mem_wb.wb_val;
    }
    return latched;
}

Operands forward_operands(const Pipeline *p, const IDEX *d) {
    Operands o = { d->rs1_val, d->rs2_val, 0, 0 };
    if (!p->forwarding || d->fault != PF_NONE) return o;

    if (d->ctrl.uses_rs1 && d->instr.rs1 != 0)
        o.a = forward_one(p, d->instr.rs1, d->rs1_val, &o.fwd_ex, &o.fwd_mem);
    if (d->ctrl.uses_rs2 && d->instr.rs2 != 0)
        o.b = forward_one(p, d->instr.rs2, d->rs2_val, &o.fwd_ex, &o.fwd_mem);
    return o;
}
