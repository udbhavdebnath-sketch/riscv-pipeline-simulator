#ifndef HAZARD_H
#define HAZARD_H

#include "pipeline_state.h"

typedef struct {
    int stall;      /* hold ID, bubble EX, freeze IF                 */
    int load_use;   /* the producer is a load still in EX (Task 3)   */
} HazardVerdict;

typedef struct {
    uint32_t a, b;        /* the operands EX should actually use */
    int fwd_ex, fwd_mem;  /* how many operands came from each path (stats) */
} Operands;

/* valid latch + really writes a register + not x0 + same register number */
int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs);

/* Called in ID. Stalling mode: stall while any of ID/EX, EX/MEM, MEM/WB
 * produces a source. Forwarding mode: stall only for load-use. */
HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c);

/* Called in EX: the forwarding muxes in front of the ALU inputs. */
Operands forward_operands(const Pipeline *p, const IDEX *d);

#endif /* HAZARD_H */
