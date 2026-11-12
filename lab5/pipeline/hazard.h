#ifndef HAZARD_H
#define HAZARD_H

#include "pipeline_state.h"

/* Forwarding mux select for one EX operand. */
typedef enum { FWD_NONE = 0, FWD_EX_MEM, FWD_MEM_WB } FwdSel;

/* The hazard unit is a plug-in, selected at run time with --hazard.
 *   stall   : called in ID with the instruction being decoded; 1 = hold it
 *             in ID this cycle (bubble into EX).
 *   forward : called in EX for each source register; says where the
 *             freshest value of `reg` comes from. */
struct HazardUnit {
    const char *name;
    const char *description;
    int    (*stall)(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c);
    FwdSel (*forward)(const Pipeline *p, int reg);
};
typedef struct HazardUnit HazardUnit;

extern const HazardUnit hazard_stall;     /* interlock only, no forwarding  */
extern const HazardUnit hazard_forward;   /* EX/MEM + MEM/WB forwarding     */

const HazardUnit *hazard_find(const char *name);

/* A valid latch whose instruction writes unified register r (r != REG_NONE). */
static inline int hz_writes(int valid, const ControlSignals *c, int r) {
    return valid && r != REG_NONE && c->dst == r;
}

#endif /* HAZARD_H */
