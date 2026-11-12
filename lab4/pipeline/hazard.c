#include "hazard.h"
#include <string.h>

/* The register file is written in WB before ID reads it in the same cycle
 * (write-first-half / read-second-half), so an instruction three or more
 * behind its producer never needs help from the hazard unit. */

/* ---------------- interlock: stall until the value is in the register file */

static int stall_interlock(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c) {
    uint32_t srcs[2] = { c->uses_rs1 ? d->rs1 : 0, c->uses_rs2 ? d->rs2 : 0 };
    for (int k = 0; k < 2; k++) {
        if (hz_writes(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  srcs[k])) return 1;
        if (hz_writes(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, srcs[k])) return 1;
    }
    return 0;
}

static FwdSel forward_none(const Pipeline *p, uint32_t reg) {
    (void)p; (void)reg;
    return FWD_NONE;
}

const HazardUnit hazard_stall = {
    "stall",
    "interlock: stall until the producer has written back, no forwarding",
    stall_interlock,
    forward_none,
};

/* ---------------- forwarding: only a load followed by a use must wait */

static int stall_load_use(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c) {
    if (!p->id_ex.valid || !p->id_ex.ctrl.mem_read) return 0;
    return (c->uses_rs1 && hz_writes(1, &p->id_ex.ctrl, p->id_ex.instr.rd, d->rs1))
        || (c->uses_rs2 && hz_writes(1, &p->id_ex.ctrl, p->id_ex.instr.rd, d->rs2));
}

static FwdSel forward_ex_mem_wb(const Pipeline *p, uint32_t reg) {
    /* The younger producer (EX/MEM) wins over the older one (MEM/WB). */
    if (hz_writes(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, reg)) return FWD_EX_MEM;
    if (hz_writes(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, reg)) return FWD_MEM_WB;
    return FWD_NONE;
}

const HazardUnit hazard_forward = {
    "forward",
    "forwarding from EX/MEM and MEM/WB, one stall on load-use",
    stall_load_use,
    forward_ex_mem_wb,
};

const HazardUnit *hazard_find(const char *name) {
    if (strcmp(name, hazard_stall.name) == 0)   return &hazard_stall;
    if (strcmp(name, hazard_forward.name) == 0) return &hazard_forward;
    return NULL;
}
