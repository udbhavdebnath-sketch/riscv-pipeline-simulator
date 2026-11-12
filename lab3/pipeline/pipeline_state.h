#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "control.h"

/* Faults are carried down the pipe and only become a CPUStatus in WB
 * (precise exceptions). */
#define PF_NONE    0
#define PF_ILLEGAL 1
#define PF_FETCH   2
#define PF_MEM     3

/* Why a latch is empty. Bubbles are counted when they reach WB, so every
 * cycle is either a retirement, a stall, a squash, or one of the 4 fill
 * cycles: cycles = retired + 4 + 2 x redirects + stalls. */
#define BUB_FILL        0
#define BUB_STALL       1
#define BUB_SQUASH_HEAD 2   /* older of the two instructions a redirect kills */
#define BUB_SQUASH      3

typedef struct {
    int valid; int bubble; uint32_t pc; uint32_t raw; int fault;
} IFID;

typedef struct {
    int valid; int bubble; uint32_t pc; int fault;
    DecodedInstr instr; const InstructionHandler *handler; ControlSignals ctrl;
    uint32_t rs1_val, rs2_val;
} IDEX;

typedef struct {
    int valid; int bubble; uint32_t pc; int fault;
    DecodedInstr instr; const InstructionHandler *handler; ControlSignals ctrl;
    uint32_t alu_result; uint32_t store_val; uint32_t fault_addr;
    int nfwd;                 /* operands forwarded in EX, counted at retire */
} EXMEM;

typedef struct {
    int valid; int bubble; uint32_t pc; int fault;
    DecodedInstr instr; const InstructionHandler *handler; ControlSignals ctrl;
    uint32_t wb_val; uint32_t fault_addr;
    int nfwd;
} MEMWB;

struct HazardUnit;

typedef struct {
    IFID if_id;  IDEX id_ex;  EXMEM ex_mem;  MEMWB mem_wb;
    const struct HazardUnit *hazard;
    uint64_t cycles, retired, stalls, forwards;
    uint64_t branches, taken, jal, jalr, redirects, squashed;
} Pipeline;

extern int pipeline_trace;   /* --trace: per-cycle stage dump */

void      pipeline_init (Pipeline *p, const struct HazardUnit *hz);
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem);
void      pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles);

#endif /* PIPELINE_STATE_H */
