#include "pipeline_state.h"
#include "hazard.h"
#include "decoder.h"
#include "cli_display.h"
#include <stdio.h>
#include <string.h>

#define OP_LOAD  0x03
#define OP_STORE 0x23
#define OP_LUI   0x37
#define OP_AUIPC 0x17
#define OP_JAL   0x6F
#define OP_JALR  0x67

int pipeline_trace = 0;

void pipeline_init(Pipeline *p, const HazardUnit *hz, const BranchPredictor *bp) {
    memset(p, 0, sizeof(*p));
    p->hazard = hz;
    p->bp = bp;
    bp->reset(&p->bps);
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

/* The forwarding mux in front of one ALU input. */
static uint32_t operand(const Pipeline *p, int reg, uint32_t latched, int *nfwd) {
    if (reg == REG_NONE) return latched;
    switch (p->hazard->forward(p, reg)) {
        case FWD_EX_MEM: (*nfwd)++; return p->ex_mem.alu_result;
        case FWD_MEM_WB: (*nfwd)++; return p->mem_wb.wb_val;
        default:         return latched;
    }
}

/* IF pre-decode: only the opcode and the immediate are looked at. It stands
 * in for a branch target buffer that always hits, so only the direction of
 * a conditional branch is ever guessed. jal is redirected for free; jalr is
 * not predicted at all. */
static uint32_t predict_next(const Pipeline *p, uint32_t pc, uint32_t raw) {
    DecodedInstr d;
    const InstructionHandler *h;
    if (decode_instruction(raw, &d, &h) != DECODE_OK) return pc + 4;
    if (h->format == FMT_J) return pc + (uint32_t)d.imm;
    if (h->format == FMT_B) {
        uint32_t target = pc + (uint32_t)d.imm;
        return p->bp->predict(&p->bps, pc, target) ? target : pc + 4;
    }
    return pc + 4;
}

/* The register files, addressed by unified number. */
static uint32_t read_unified(const CPUState *cpu, int r) {
    if (r == REG_NONE) return 0;
    return (r >= REG_F0) ? cpu_read_freg(cpu, r - REG_F0) : cpu_read_reg(cpu, r);
}
static void write_unified(CPUState *cpu, int r, uint32_t v) {
    if (r == REG_NONE) return;
    if (r >= REG_F0) cpu_write_freg(cpu, r - REG_F0, v); else cpu_write_reg(cpu, r, v);
}

/* Runs an extension instruction's own handler in EX, on a scratch copy of
 * the machine whose source registers hold the latched-or-forwarded operands.
 * Nobody on this path knows what a float is. fcsr is read and written right
 * here: instructions reach EX one at a time, in order, and are never
 * squashed after it, so fcsr needs no forwarding and no stall. */
static void execute_handler(const IDEX *d, EXMEM *o, CPUState *cpu, Memory *mem,
                            uint32_t a, uint32_t b, uint32_t c) {
    CPUState s = *cpu;
    s.pc = d->pc;
    s.status = CPU_RUNNING;
    const ControlSignals *k = &d->ctrl;
    if (k->src1 != REG_NONE) write_unified(&s, k->src1, a);
    if (k->src2 != REG_NONE) write_unified(&s, k->src2, b);
    if (k->src3 != REG_NONE) write_unified(&s, k->src3, c);
    DecodedInstr in = d->instr;
    d->handler->execute(&s, mem, &in);
    if (s.status != CPU_RUNNING) {          /* e.g. reserved rounding mode */
        o->fault = (s.status == CPU_MEM_FAULT) ? PF_MEM : PF_ILLEGAL;
        o->fault_addr = s.fault_addr;
        snprintf(o->msg, sizeof(o->msg), "%s", s.fault_msg);
        return;
    }
    o->alu_result = read_unified(&s, k->dst);
    cpu->fcsr = s.fcsr;
}

/* An older instruction is stopping the machine in WB. The one behind it
 * already ran EX and may have touched fcsr: put fcsr back. */
static void undo_younger_fcsr(const Pipeline *p, CPUState *cpu) {
    if (p->ex_mem.valid) cpu->fcsr = p->ex_mem.fcsr_before;
}

static void trace_pc(int valid, uint32_t pc) {
    if (valid) printf(" 0x%08x", pc); else printf(" ----------");
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;
    Pipeline nx = *p;        /* nx = next state; p = current (old) state */

    /* ---------------- WB ---------------- */
    if (p->mem_wb.valid && p->mem_wb.fault != PF_NONE) {
        const MEMWB *w = &p->mem_wb;
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
        if (w->msg[0]) snprintf(cpu->fault_msg, sizeof(cpu->fault_msg), "%s", w->msg);
        undo_younger_fcsr(p, cpu);
        *p = nx;                /* the faulting cycle is not counted */
        return cpu->status;
    }
    nx.cycles++;
    if (!p->mem_wb.valid) {
        if (p->mem_wb.bubble == BUB_STALL) nx.stalls++;
        if (p->mem_wb.bubble == BUB_SQUASH_HEAD) { nx.redirects++; nx.squashed += 2; }
    } else {
        const MEMWB *w = &p->mem_wb;
        write_unified(cpu, w->ctrl.dst, w->wb_val);
        if (w->ctrl.is_system) {
            uint32_t saved = cpu->pc;
            DecodedInstr in = w->instr;
            cpu->pc = w->pc;
            w->handler->execute(cpu, mem, &in);
            cpu->pc = saved;
        }
        nx.retired++;
        nx.forwards += (uint64_t)w->nfwd;
        if (w->ctrl.is_branch) nx.branches++;
        if (w->mispredict) nx.mispredicts++;
        if (w->instr.opcode == OP_JAL)  nx.jal++;
        if (w->instr.opcode == OP_JALR) nx.jalr++;
        if (cpu->status != CPU_RUNNING) {   /* ecall exit: nothing younger may commit */
            undo_younger_fcsr(p, cpu);
            *p = nx;
            return cpu->status;
        }
    }

    /* ---------------- MEM ---------------- */
    nx.mem_wb.valid = 0;
    nx.mem_wb.bubble = p->ex_mem.bubble;
    if (p->ex_mem.valid) {
        const EXMEM *e = &p->ex_mem;
        MEMWB *o = &nx.mem_wb;
        o->valid = 1; o->pc = e->pc; o->fault = e->fault; o->instr = e->instr;
        o->handler = e->handler; o->ctrl = e->ctrl; o->wb_val = e->alu_result;
        o->fault_addr = e->fault_addr; o->nfwd = e->nfwd; o->mispredict = e->mispredict;
        memcpy(o->msg, e->msg, sizeof(o->msg));
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
    /* Branches and jumps resolve here. If the real next PC differs from the
     * one IF guessed, the two younger instructions (in ID and IF) are
     * squashed and fetch restarts at the right address next cycle: 2 cycles.
     * The predictor learns only here, where `taken` is a fact. */
    int redirect = 0;
    uint32_t redirect_pc = 0;
    nx.ex_mem.valid = 0;
    nx.ex_mem.bubble = p->id_ex.bubble;
    if (p->id_ex.valid) {
        const IDEX *d = &p->id_ex;
        EXMEM *o = &nx.ex_mem;
        o->valid = 1; o->pc = d->pc; o->fault = d->fault; o->instr = d->instr;
        o->handler = d->handler; o->ctrl = d->ctrl;
        o->alu_result = 0; o->fault_addr = 0; o->nfwd = 0; o->mispredict = 0;
        o->fcsr_before = cpu->fcsr; o->msg[0] = '\0';
        if (d->fault == PF_NONE) {
            uint32_t a = operand(p, d->ctrl.src1, d->rs1_val, &o->nfwd);
            uint32_t b = operand(p, d->ctrl.src2, d->rs2_val, &o->nfwd);
            uint32_t c = operand(p, d->ctrl.src3, d->rs3_val, &o->nfwd);
            o->store_val = b;
            uint32_t next = d->pc + 4;
            if (d->ctrl.is_branch) {
                uint32_t target = d->pc + (uint32_t)d->instr.imm;
                int taken = branch_taken(d->instr.funct3, a, b);
                if (taken) next = target;
                p->bp->update(&nx.bps, d->pc, target, taken);
                o->mispredict = (next != d->pred_next);
            } else if (d->ctrl.is_jump) {
                o->alu_result = d->pc + 4;
                next = (d->instr.opcode == OP_JAL)
                     ? d->pc + (uint32_t)d->instr.imm
                     : (a + (uint32_t)d->instr.imm) & ~1u;
            } else if (d->ctrl.exec_in_ex) {
                execute_handler(d, o, cpu, mem, a, b, c);
            } else if (d->ctrl.mem_read || d->ctrl.mem_write) {
                o->alu_result = a + (uint32_t)d->instr.imm;    /* effective address */
            } else {
                o->alu_result = alu(&d->instr, d->pc, a, b);
            }
            if (next != d->pred_next) {
                redirect = 1;
                redirect_pc = next;
            }
        }
    }

    /* ---------------- ID ---------------- */
    int stall = 0;
    nx.id_ex.valid = 0;
    nx.id_ex.bubble = p->if_id.bubble;
    if (redirect) {
        nx.id_ex.bubble = BUB_SQUASH_HEAD;  /* kill the instruction in ID */
    } else if (p->if_id.valid) {
        IDEX *o = &nx.id_ex;
        memset(o, 0, sizeof(*o));
        o->valid = 1; o->pc = p->if_id.pc; o->pred_next = p->if_id.pred_next;
        DecodedInstr d; const InstructionHandler *h = NULL;
        memset(&d, 0, sizeof(d));
        if (p->if_id.fault != PF_NONE) {
            o->fault = p->if_id.fault;
        } else if (decode_instruction(p->if_id.raw, &d, &h) != DECODE_OK) {
            o->fault = PF_ILLEGAL;
            o->instr.raw = p->if_id.raw;
        } else {
            ControlSignals c = control_decode(&d, h);
            if (p->hazard->stall(p, &d, &c)) {
                stall = 1;
                o->valid = 0;             /* bubble into EX */
                o->bubble = BUB_STALL;
            } else {
                /* WB already wrote this cycle, so this read sees it. */
                o->instr = d; o->handler = h; o->ctrl = c;
                o->rs1_val = read_unified(cpu, c.src1);
                o->rs2_val = read_unified(cpu, c.src2);
                o->rs3_val = read_unified(cpu, c.src3);
            }
        }
    }

    /* ---------------- IF ---------------- */
    uint32_t fetch_pc = cpu->pc;
    if (stall) {
        nx.if_id = p->if_id;              /* freeze IF/ID and the fetch PC */
    } else {
        uint32_t raw = 0;
        nx.if_id.valid = 1;
        nx.if_id.bubble = BUB_FILL;
        nx.if_id.pc = fetch_pc;
        nx.if_id.fault = (memory_read_word(mem, fetch_pc, &raw) == MEM_OK) ? PF_NONE : PF_FETCH;
        nx.if_id.raw = raw;
        nx.if_id.pred_next = (nx.if_id.fault == PF_NONE) ? predict_next(p, fetch_pc, raw) : fetch_pc + 4;
        cpu->pc = nx.if_id.pred_next;
    }
    if (redirect) {                       /* kill what IF just fetched */
        nx.if_id.valid = 0;
        nx.if_id.bubble = BUB_SQUASH;
        cpu->pc = redirect_pc;
    }

    if (pipeline_trace) {
        printf("[cycle %llu] IF", (unsigned long long)nx.cycles);
        trace_pc(1, fetch_pc);
        printf(" | ID");  trace_pc(p->if_id.valid,  p->if_id.pc);
        printf(" | EX");  trace_pc(p->id_ex.valid,  p->id_ex.pc);
        printf(" | MEM"); trace_pc(p->ex_mem.valid, p->ex_mem.pc);
        printf(" | WB");  trace_pc(p->mem_wb.valid, p->mem_wb.pc);
        printf("\n");
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
    printf("pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n", p->hazard->name, p->bp->name);
    printf("retired         = %llu\n", (unsigned long long)p->retired);
    printf("CPI             = %.3f\n", p->retired ? (double)p->cycles / (double)p->retired : 0.0);
    printf("stalls          = %llu\n", (unsigned long long)p->stalls);
    printf("forwards        = %llu\n", (unsigned long long)p->forwards);
    printf("branches        = %-9llu conditional branches executed\n", (unsigned long long)p->branches);
    printf("mispredicts     = %-9llu ... of which the guess was wrong\n", (unsigned long long)p->mispredicts);
    printf("accuracy        = %.1f%%\n", p->branches ? 100.0 * (double)(p->branches - p->mispredicts) / (double)p->branches : 100.0);
    printf("jal             = %-9llu redirected in IF: free\n", (unsigned long long)p->jal);
    printf("jalr            = %-9llu never predicted: 2 cycles each\n", (unsigned long long)p->jalr);
    printf("redirects       = %-9llu mispredicts + jalr\n", (unsigned long long)p->redirects);
    printf("squashed        = %-9llu instructions thrown away (2 per redirect)\n", (unsigned long long)p->squashed);
}
