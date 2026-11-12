#ifndef ZICSR_H
#define ZICSR_H

#define CSR_FFLAGS 0x001
#define CSR_FRM    0x002
#define CSR_FCSR   0x003

/* Registers csrrw, csrrs, csrrc and their immediate forms. Only the three
 * floating-point CSRs exist; any other CSR number is an illegal instruction. */
void register_zicsr(void);

#endif /* ZICSR_H */
