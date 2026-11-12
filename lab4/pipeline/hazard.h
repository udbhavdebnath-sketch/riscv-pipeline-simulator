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
    FwdSel (*forward)(const Pipeline *p, uint32_t reg);
};
typedef struct HazardUnit HazardUnit;

extern const HazardUnit hazard_stall;     /* interlock only, no forwarding  */
extern const HazardUnit hazard_forward;   /* EX/MEM + MEM/WB forwarding     */

const HazardUnit *hazard_find(const char *name);

/* rd of a valid, register-writing latch matches r (r != x0). Shared by both units. */
static inline int hz_writes(int valid, const ControlSignals *c, uint32_t rd, uint32_t r) {
    return valid && c->reg_write && rd != 0 && rd == r;
}

#endif /* HAZARD_H */
