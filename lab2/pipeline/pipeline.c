#include "pipeline_state.h"
#include "decoder.h"
#include "cli_display.h"
#include <stdio.h>
#include <string.h>

#define OP_LOAD  0x03
#define OP_STORE 0x23
#define OP_LUI   0x37
#define OP_AUIPC 0x17
#define OP_JAL   0x6F

int pipeline_trace = 0;

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(*p));
}

static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    switch (i->opcode) {
        case OP_LUI:   return (uint32_t)i->imm;
        case OP_AUIPC: return pc + (uint32_t)i->imm;
        case OP_LOAD:
        case OP_STORE: return a + (uint32_t)i->imm;
        case 0x13: b = (uint32_t)i->imm; /* I-type: same ALU, immediate as b */
            /* fall through */
        case 0x33: {
            int is_r = (i->opcode == 0x33);
            switch (i->funct3) {
                case 0x0: return (is_r && i->funct7 == 0x20) ? a - b : a + b;
                case 0x1: return a << (b & 0x1F);
                case 0x2: return ((int32_t)a < (int32_t)b) ? 1 : 0;
                case 0x3: return (a < b) ? 1 : 0;
                case 0x4: return a ^ b;
                case 0x5: return (i->funct7 == 0x20) ? (uint32_t)((int32_t)a >> (b & 0x1F))
                                                     : a >> (b & 0x1F);
                case 0x6: return a | b;
                case 0x7: return a & b;
            }
        }
    }
    return 0;
}

static int branch_taken(uint32_t funct3, uint32_t a, uint32_t b) {
    switch (funct3) {
        case 0x0: return a == b;
        case 0x1: return a != b;
        case 0x4: return (int32_t)a <  (int32_t)b;
        case 0x5: return (int32_t)a >= (int32_t)b;
        case 0x6: return a <  b;
        case 0x7: return a >= b;
    }
    return 0;
}

/* Naive 3-latch hazard: stall while any older in-flight instruction will
 * write a register this one reads. No forwarding. */
static int writes_reg(int valid, const ControlSignals *c, uint32_t rd, uint32_t r) {
    return valid && c->reg_write && rd != 0 && rd == r;
}
static int hazard(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c) {
    uint32_t srcs[2] = { c->uses_rs1 ? d->rs1 : 0, c->uses_rs2 ? d->rs2 : 0 };
    for (int k = 0; k < 2; k++) {
        uint32_t r = srcs[k];
        if (r == 0) continue;
        if (writes_reg(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  r)) return 1;
        if (writes_reg(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, r)) return 1;
        if (writes_reg(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, r)) return 1;
    }
    return 0;
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;
    Pipeline nx = *p;        /* nx = next state; p = current (old) state */

    /* ---------------- WB ---------------- */
    if (p->mem_wb.valid) {
        const MEMWB *w = &p->mem_wb;
        if (w->fault != PF_NONE) {
            if (w->fault == PF_MEM) {
                cpu->status = CPU_MEM_FAULT;
                cpu->fault_addr = w->fault_addr;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "memory access out of bounds at address 0x%08x (pc=0x%08x)", w->fault_addr, w->pc);
            } else if (w->fault == PF_FETCH) {
                cpu->status = CPU_MEM_FAULT;
                cpu->fault_addr = w->pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "instruction fetch out of bounds at pc=0x%08x", w->pc);
            } else {
                cpu->status = CPU_ILLEGAL_INSTRUCTION;
                cpu->fault_addr = w->pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "unknown instruction 0x%08x at pc=0x%08x (no handler registered)",
                         w->instr.raw, w->pc);
            }
            *p = nx;                /* the faulting cycle is not counted */
            return cpu->status;
        }
    }
    nx.cycles++;
    if (p->mem_wb.valid) {
        const MEMWB *w = &p->mem_wb;
        if (w->ctrl.reg_write) cpu_write_reg(cpu, (int)w->instr.rd, w->wb_val);
        if (w->ctrl.is_system) {
            uint32_t saved = cpu->pc;
            DecodedInstr in = w->instr;
            cpu->pc = w->pc;
            w->handler->execute(cpu, mem, &in);
            cpu->pc = saved;
        }
        nx.retired++;
        if (cpu->status != CPU_RUNNING) {   /* ecall exit: nothing younger may commit */
            *p = nx;
            return cpu->status;
        }
    }

    /* ---------------- MEM ---------------- */
    nx.mem_wb.valid = 0;
    if (p->ex_mem.valid) {
        const EXMEM *e = &p->ex_mem;
        MEMWB *o = &nx.mem_wb;
        o->valid = 1; o->pc = e->pc; o->fault = e->fault; o->instr = e->instr;
        o->handler = e->handler; o->ctrl = e->ctrl; o->wb_val = e->alu_result;
        o->fault_addr = e->fault_addr;
        if (e->fault == PF_NONE) {
            uint32_t a = e->alu_result;
            MemStatus st = MEM_OK;
            if (e->ctrl.mem_read) {
                uint8_t b; uint16_t h; uint32_t wv = 0;
                switch (e->instr.funct3) {
                    case 0x0: st = memory_read_byte(mem, a, &b); wv = (uint32_t)(int32_t)(int8_t)b; break;
                    case 0x1: st = memory_read_half(mem, a, &h); wv = (uint32_t)(int32_t)(int16_t)h; break;
                    case 0x2: st = memory_read_word(mem, a, &wv); break;
                    case 0x4: st = memory_read_byte(mem, a, &b); wv = b; break;
                    case 0x5: st = memory_read_half(mem, a, &h); wv = h; break;
                }
                o->wb_val = wv;
            } else if (e->ctrl.mem_write) {
                switch (e->instr.funct3) {
                    case 0x0: st = memory_write_byte(mem, a, (uint8_t)e->store_val); break;
                    case 0x1: st = memory_write_half(mem, a, (uint16_t)e->store_val); break;
                    case 0x2: st = memory_write_word(mem, a, e->store_val); break;
                }
            }
            if (st != MEM_OK) { o->fault = PF_MEM; o->fault_addr = a; }
        }
    }

    /* ---------------- EX ---------------- */
    int flush = 0;
    uint32_t redirect_pc = 0;
    nx.ex_mem.valid = 0;
    if (p->id_ex.valid) {
        const IDEX *d = &p->id_ex;
        EXMEM *o = &nx.ex_mem;
        o->valid = 1; o->pc = d->pc; o->fault = d->fault; o->instr = d->instr;
        o->handler = d->handler; o->ctrl = d->ctrl; o->store_val = d->rs2_val;
        o->alu_result = 0; o->fault_addr = 0;
        if (d->fault == PF_NONE) {
            if (d->ctrl.is_branch) {
                if (branch_taken(d->instr.funct3, d->rs1_val, d->rs2_val)) {
                    flush = 1;
                    redirect_pc = d->pc + (uint32_t)d->instr.imm;
                }
            } else if (d->ctrl.is_jump) {
                o->alu_result = d->pc + 4;
                flush = 1;
                redirect_pc = (d->instr.opcode == OP_JAL)
                            ? d->pc + (uint32_t)d->instr.imm
                            : (d->rs1_val + (uint32_t)d->instr.imm) & ~1u;
            } else {
                o->alu_result = alu(&d->instr, d->pc, d->rs1_val, d->rs2_val);
            }
        }
    }

    /* ---------------- ID ---------------- */
    int stall = 0;
    nx.id_ex.valid = 0;
    if (flush) nx.flushes++;          /* the instruction in ID is squashed */
    if (p->if_id.valid && !flush) {   /* a flush kills the instruction in ID */
        IDEX *o = &nx.id_ex;
        memset(o, 0, sizeof(*o));
        o->valid = 1; o->pc = p->if_id.pc;
        DecodedInstr d; const InstructionHandler *h = NULL;
        memset(&d, 0, sizeof(d));
        if (p->if_id.fault != PF_NONE) {
            o->fault = p->if_id.fault;
        } else if (decode_instruction(p->if_id.raw, &d, &h) != DECODE_OK) {
            o->fault = PF_ILLEGAL;
            o->instr.raw = p->if_id.raw;
        } else {
            ControlSignals c = control_decode(&d, h);
            if (hazard(p, &d, &c)) {
                stall = 1;
                o->valid = 0;             /* bubble into EX */
                nx.stalls++;
            } else {
                o->instr = d; o->handler = h; o->ctrl = c;
                o->rs1_val = cpu_read_reg(cpu, (int)d.rs1);
                o->rs2_val = cpu_read_reg(cpu, (int)d.rs2);
            }
        }
    }

    /* ---------------- IF ---------------- */
    if (stall) {
        nx.if_id = p->if_id;              /* freeze IF/ID and the fetch PC */
    } else {
        uint32_t fetch_pc = flush ? redirect_pc : cpu->pc;
        uint32_t raw = 0;
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        nx.if_id.fault = (memory_read_word(mem, fetch_pc, &raw) == MEM_OK) ? PF_NONE : PF_FETCH;
        nx.if_id.raw = raw;
        cpu->pc = fetch_pc + 4;
    }

    if (pipeline_trace) {
        fprintf(stderr, "c%-4llu IF:%08x ID:%c EX:%c MEM:%c WB:%c %s%s\n",
                (unsigned long long)nx.cycles, nx.if_id.pc,
                nx.id_ex.valid ? '*' : '.', nx.ex_mem.valid ? '*' : '.',
                nx.mem_wb.valid ? '*' : '.', p->mem_wb.valid ? '*' : '.',
                stall ? "STALL " : "", flush ? "FLUSH" : "");
    }

    *p = nx;                 /* commit: one clock edge */
    return cpu->status;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles) {
        pipeline_cycle(p, cpu, mem);
    }
    cpu->cycle_count = p->cycles;
    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    if (cpu->status == CPU_RUNNING) {
        printf("  -> stopped: reached max cycle limit (%llu)\n", (unsigned long long)max_cycles);
    }
    printf("pipeline        = 5-stage, naive 3-latch stall, branches resolved in EX\n");
    printf("cycles          = %llu\n", (unsigned long long)p->cycles);
    printf("retired         = %llu\n", (unsigned long long)p->retired);
    printf("stalls          = %llu\n", (unsigned long long)p->stalls);
    printf("flushes         = %llu\n", (unsigned long long)p->flushes);
    printf("CPI             = %.3f\n", p->retired ? (double)p->cycles / (double)p->retired : 0.0);
}
