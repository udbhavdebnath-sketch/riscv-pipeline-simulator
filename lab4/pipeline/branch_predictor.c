#include "branch_predictor.h"
#include <string.h>

static const BranchPredictor *const bp_table[] = {
    &bp_not_taken, &bp_btfn, &bp_1bit, &bp_2bit,
};

const BranchPredictor *bp_find(const char *name) {
    for (unsigned k = 0; k < sizeof(bp_table) / sizeof(bp_table[0]); k++) {
        if (strcmp(bp_table[k]->name, name) == 0) return bp_table[k];
    }
    return NULL;
}
