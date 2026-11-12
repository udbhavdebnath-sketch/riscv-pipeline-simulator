#include "assembler.h"
#include "isa_registry.h"
#include "cpu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES     8192
#define MAX_LINE_LEN  512
#define MAX_SYMBOLS   4096

typedef struct {
    char text[MAX_LINE_LEN];
    int  lineno;
} SrcLine;

typedef struct {
    char name[64];
    uint32_t address;
} Symbol;

typedef struct {
    Symbol items[MAX_SYMBOLS];
    int count;
} SymbolTable;

static int symtab_find(SymbolTable *st, const char *name) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->items[i].name, name) == 0) return i;
    return -1;
}

static int symtab_add(SymbolTable *st, const char *name, uint32_t addr) {
    if (symtab_find(st, name) != -1) return -1; /* duplicate */
    if (st->count >= MAX_SYMBOLS) return -2;
    strncpy(st->items[st->count].name, name, sizeof(st->items[st->count].name) - 1);
    st->items[st->count].name[sizeof(st->items[st->count].name) - 1] = '\0';
    st->items[st->count].address = addr;
    st->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Small string helpers                                               */
/* ------------------------------------------------------------------ */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return s;
}

static void strip_comment(char *s) {
    for (char *p = s; *p; p++) {
        if (*p == '#') { *p = '\0'; break; }
        if (*p == '/' && *(p + 1) == '/') { *p = '\0'; break; }
    }
}

static void to_lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Splits "a, b, c" into up to max_tokens trimmed tokens. Commas are never
 * expected to appear inside our operand syntax (offsets use "imm(reg)"
 * with no comma), so a plain split is sufficient. Returns token count. */
static int split_commas(char *s, char *tokens[], int max_tokens) {
    int n = 0;
    char *tok = strtok(s, ",");
    while (tok && n < max_tokens) {
        tokens[n++] = trim(tok);
        tok = strtok(NULL, ",");
    }
    return n;
}

/* label:            -> label="label", rest=""
 * label: mnemonic .. -> label="label", rest="mnemonic .."
 * mnemonic ..        -> label="",      rest="mnemonic .."
 */
static void tokenize_line(char *line, char *label_out, char *mnemonic_out, char *operand_out) {
    label_out[0] = mnemonic_out[0] = operand_out[0] = '\0';

    strip_comment(line);
    char *s = trim(line);
    if (*s == '\0') return;

    char *colon = strchr(s, ':');
    if (colon) {
        *colon = '\0';
        char *lbl = trim(s);
        strncpy(label_out, lbl, 63); label_out[63] = '\0';
        s = trim(colon + 1);
    }
    if (*s == '\0') return;

    char *space = s;
    while (*space && !isspace((unsigned char)*space)) space++;
    if (*space) {
        *space = '\0';
        strncpy(mnemonic_out, s, 31); mnemonic_out[31] = '\0';
        strncpy(operand_out, trim(space + 1), 255); operand_out[255] = '\0';
    } else {
        strncpy(mnemonic_out, s, 31); mnemonic_out[31] = '\0';
    }
    to_lower_inplace(mnemonic_out);
}

/* ------------------------------------------------------------------ */
/* Value / operand parsing                                            */
/* ------------------------------------------------------------------ */

static int is_pseudo_directive(const char *mnemonic) {
    return strcmp(mnemonic, ".text") == 0 || strcmp(mnemonic, ".data") == 0 ||
           strcmp(mnemonic, ".globl") == 0 || strcmp(mnemonic, ".global") == 0;
}

static int set_err(AsmError *err, int lineno, AsmStatus status, const char *fmt, ...);
/* forward-declared helper defined near the bottom; returns `status` so
 * call sites can `return set_err(...)` directly */

/* Parses a decimal/hex literal or a symbol reference. If pc_relative is
 * non-zero and the token resolves to a symbol, the result is
 * (symbol_address - addr) -- used for branch/jal targets. */
static int parse_value(const char *token, SymbolTable *st, uint32_t addr,
                        int pc_relative, int32_t *out, AsmError *err, int lineno) {
    if (!token || token[0] == '\0') { *out = 0; return ASM_OK; }

    char *endptr;
    long v = strtol(token, &endptr, 0); /* base 0: handles "0x..", "-5", "10" */
    if (*endptr == '\0') {
        *out = (int32_t)v;
        return ASM_OK;
    }

    int idx = symtab_find(st, token);
    if (idx == -1) {
        return set_err(err, lineno, ASM_ERR_UNDEFINED_SYMBOL, "undefined symbol '%s'", token);
    }
    uint32_t sym_addr = st->items[idx].address;
    *out = pc_relative ? (int32_t)(sym_addr - addr) : (int32_t)sym_addr;
    return ASM_OK;
}

static int parse_register(const char *token, uint32_t *out, AsmError *err, int lineno) {
    int idx = cpu_reg_index_from_name(token);
    if (idx < 0) return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a valid register name", token);
    *out = (uint32_t)idx;
    return ASM_OK;
}

/* An x or an f register, as the instruction's IF_* flag for that operand says. */
static int parse_reg_kind(const char *token, int is_fp, uint32_t *out, AsmError *err, int lineno) {
    if (!is_fp) return parse_register(token, out, err, lineno);
    int idx = cpu_freg_index_from_name(token);
    if (idx < 0) return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a valid f register name", token);
    *out = (uint32_t)idx;
    return ASM_OK;
}

/* Optional trailing rounding-mode operand: rne rtz rdn rup rmm dyn. */
static int parse_rm(const char *token, int32_t *out, AsmError *err, int lineno) {
    static const char *names[8] = {"rne", "rtz", "rdn", "rup", "rmm", NULL, NULL, "dyn"};
    for (int k = 0; k < 8; k++) {
        if (names[k] && strcmp(token, names[k]) == 0) { *out = k; return ASM_OK; }
    }
    return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a rounding mode", token);
}

/* A CSR operand: a name (fflags, frm, fcsr) or a number. */
static int parse_csr(const char *token, int32_t *out, AsmError *err, int lineno) {
    if (strcmp(token, "fflags") == 0) { *out = 0x001; return ASM_OK; }
    if (strcmp(token, "frm") == 0)    { *out = 0x002; return ASM_OK; }
    if (strcmp(token, "fcsr") == 0)   { *out = 0x003; return ASM_OK; }
    char *end;
    long v = strtol(token, &end, 0);
    if (*token && *end == '\0' && v >= 0 && v <= 0xFFF) { *out = (int32_t)v; return ASM_OK; }
    return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "'%s' is not a CSR", token);
}

/* Parses "imm(reg)" e.g. "0(sp)", "-4(t0)", "(a0)". */
static int parse_offset_paren(const char *token, SymbolTable *st, uint32_t addr,
                               int32_t *imm_out, uint32_t *reg_out, AsmError *err, int lineno) {
    char buf[128];
    strncpy(buf, token, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';

    char *open = strchr(buf, '(');
    char *close = strchr(buf, ')');
    if (!open || !close || close < open) {
        return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'offset(register)', got '%s'", token);
    }
    *close = '\0';
    *open = '\0';
    char *imm_part = trim(buf);
    char *reg_part = trim(open + 1);

    int st_ok = parse_value(imm_part, st, addr, 0, imm_out, err, lineno);
    if (st_ok != ASM_OK) return st_ok;
    return parse_register(reg_part, reg_out, err, lineno);
}

typedef struct { uint32_t rd, rs1, rs2; int32_t imm; uint32_t rs3; int32_t rm; } Fields;

#define RM_DYN_DEFAULT 7   /* no rounding-mode operand: dyn, i.e. use fcsr.frm */

static int parse_fields(const InstructionHandler *h, char *operand_str, SymbolTable *st, uint32_t addr,
                         Fields *f, AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[6];
    int n = split_commas(buf, tok, 6);
    memset(f, 0, sizeof(*f));
    f->rm = RM_DYN_DEFAULT;

    uint32_t fl = h->flags;
    int rd_fp = (fl & IF_RD_FP) != 0, rs1_fp = (fl & IF_RS1_FP) != 0;
    int rs2_fp = (fl & IF_RS2_FP) != 0, rs3_fp = (fl & IF_RS3_FP) != 0;

    /* A trailing rounding mode is optional wherever funct3 holds one. */
    int nregs = (h->format == FMT_R4) ? 4 : (h->format == FMT_R2) ? 2 : 3;
    if ((fl & IF_HAS_RM) && n == nregs + 1) {
        if (parse_rm(tok[n - 1], &f->rm, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        n--;
    }

    switch (h->format) {
        case FMT_R:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, rs2'");
            if (parse_reg_kind(tok[0], rd_fp,  &f->rd,  err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[2], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_R2:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1'");
            if (parse_reg_kind(tok[0], rd_fp,  &f->rd,  err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            f->rs2 = (uint32_t)h->rs2_sel;
            return ASM_OK;

        case FMT_R4:
            if (n != 4) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, rs2, rs3'");
            if (parse_reg_kind(tok[0], rd_fp,  &f->rd,  err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[1], rs1_fp, &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[2], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_reg_kind(tok[3], rs3_fp, &f->rs3, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return ASM_OK;

        case FMT_CSR:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, csr, rs1' or 'rd, csr, uimm'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_csr(tok[1], &f->imm, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (h->funct3 >= 5) {               /* immediate forms: rs1 field is a 5-bit uimm */
                int32_t v;
                int st_ok = parse_value(tok[2], st, addr, 0, &v, err, lineno);
                if (st_ok != ASM_OK) return st_ok;
                if (v < 0 || v > 31) return set_err(err, lineno, ASM_ERR_BAD_OPERAND, "CSR immediate must be 0..31");
                f->rs1 = (uint32_t)v;
                return ASM_OK;
            }
            return parse_register(tok[2], &f->rs1, err, lineno);

        case FMT_I:
        case FMT_I_SHAMT:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, rs1, imm'");
            if (parse_register(tok[0], &f->rd, err, lineno)  != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[2], st, addr, 0, &f->imm, err, lineno);

        case FMT_I_LOAD:
        case FMT_I_JALR:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, offset(rs1)'");
            if (parse_reg_kind(tok[0], rd_fp, &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_offset_paren(tok[1], st, addr, &f->imm, &f->rs1, err, lineno);

        case FMT_I_SYS:
            return ASM_OK; /* no operands */

        case FMT_S:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rs2, offset(rs1)'");
            if (parse_reg_kind(tok[0], rs2_fp, &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_offset_paren(tok[1], st, addr, &f->imm, &f->rs1, err, lineno);

        case FMT_B:
            if (n != 3) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rs1, rs2, label'");
            if (parse_register(tok[0], &f->rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            if (parse_register(tok[1], &f->rs2, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[2], st, addr, 1, &f->imm, err, lineno);

        case FMT_U:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, imm'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[1], st, addr, 0, &f->imm, err, lineno);

        case FMT_J:
            if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'rd, label'");
            if (parse_register(tok[0], &f->rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
            return parse_value(tok[1], st, addr, 1, &f->imm, err, lineno);
    }
    return set_err(err, lineno, ASM_ERR_SYNTAX, "unhandled instruction format");
}

/* ------------------------------------------------------------------ */
/* Generic encoder -- mirrors decoder.c exactly, in reverse.          */
/* Works for ANY registered instruction (base or extension), since it */
/* only depends on the handler's opcode/funct3/funct7/format.         */
/* ------------------------------------------------------------------ */

static uint32_t encode_instruction(const InstructionHandler *h, Fields f) {
    uint32_t opcode = h->opcode;
    uint32_t funct3 = (h->funct3 >= 0) ? (uint32_t)h->funct3 : 0;
    if (h->flags & IF_HAS_RM) funct3 = (uint32_t)f.rm & 0x7;
    uint32_t funct7 = (h->funct7 >= 0) ? (uint32_t)h->funct7 : 0;
    uint32_t imm    = (uint32_t)f.imm;

    switch (h->format) {
        case FMT_R4:
            funct7 = (f.rs3 & 0x1F) << 2;        /* rs3 | fmt=00 (single) */
            /* fall through */
        case FMT_R:
        case FMT_R2:
            return (funct7 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I:
        case FMT_I_LOAD:
        case FMT_I_JALR:
            return ((imm & 0xFFF) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I_SHAMT:
            return (funct7 << 25) | ((imm & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_I_SYS:
            return (funct3 << 12) | opcode; /* rd=0, rs1=0, imm=0 */

        case FMT_CSR:
            return ((imm & 0xFFF) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_S: {
            uint32_t imm11_5 = (imm >> 5) & 0x7F;
            uint32_t imm4_0  = imm & 0x1F;
            return (imm11_5 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | (imm4_0 << 7) | opcode;
        }

        case FMT_B: {
            uint32_t b12 = (imm >> 12) & 0x1, b11 = (imm >> 11) & 0x1;
            uint32_t b10_5 = (imm >> 5) & 0x3F, b4_1 = (imm >> 1) & 0xF;
            return (b12 << 31) | (b10_5 << 25) | ((f.rs2 & 0x1F) << 20) | ((f.rs1 & 0x1F) << 15) |
                   (funct3 << 12) | (b4_1 << 8) | (b11 << 7) | opcode;
        }

        case FMT_U:
            return ((imm & 0xFFFFF) << 12) | ((f.rd & 0x1F) << 7) | opcode;

        case FMT_J: {
            uint32_t b20 = (imm >> 20) & 0x1, b19_12 = (imm >> 12) & 0xFF;
            uint32_t b11 = (imm >> 11) & 0x1, b10_1 = (imm >> 1) & 0x3FF;
            return (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12) |
                   ((f.rd & 0x1F) << 7) | opcode;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pseudo-instructions                                                */
/* ------------------------------------------------------------------ */
/* Kept deliberately minimal. "li" always expands to exactly two real
 * instructions (lui + addi) regardless of the value's magnitude -- this
 * keeps its size fixed and known during pass 1, at the cost of occasionally
 * emitting a redundant lui for small constants. */

static int pseudo_instruction_size(const char *mnemonic) {
    if (strcmp(mnemonic, "li") == 0 || strcmp(mnemonic, "la") == 0) return 8;
    if (strcmp(mnemonic, "nop") == 0) return 4;
    if (strcmp(mnemonic, "ret") == 0) return 4;
    if (strcmp(mnemonic, "j") == 0) return 4;
    if (strcmp(mnemonic, "mv") == 0) return 4;
    return 0; /* not a pseudo-instruction */
}

static void li_split(int32_t value, uint32_t *upper20, int32_t *lower12) {
    int64_t v = value;
    int64_t upper = (v + 0x800) >> 12;
    int32_t lower = (int32_t)(v - (upper << 12));
    *upper20 = (uint32_t)(upper & 0xFFFFF);
    *lower12 = lower;
}

/* Encodes a pseudo-instruction into `mem` at `addr`, writing 4 or 8 bytes.
 * Returns ASM_OK or an error status via `err`. */
static int assemble_pseudo(const char *mnemonic, char *operand_str, SymbolTable *st,
                            uint32_t addr, Memory *mem, AsmError *err, int lineno) {
    char buf[256];
    strncpy(buf, operand_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[3];
    int n = split_commas(buf, tok, 3);

    if (strcmp(mnemonic, "nop") == 0) {
        const InstructionHandler *h = isa_find_by_mnemonic("addi");
        Fields f = {0, 0, 0, 0, 0, RM_DYN_DEFAULT};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "ret") == 0) {
        const InstructionHandler *h = isa_find_by_mnemonic("jalr");
        Fields f = {0, 1 /* ra */, 0, 0, 0, RM_DYN_DEFAULT};
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "mv") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'mv rd, rs'");
        const InstructionHandler *h = isa_find_by_mnemonic("addi");
        Fields f = {0, 0, 0, 0, 0, RM_DYN_DEFAULT};
        if (parse_register(tok[0], &f.rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        if (parse_register(tok[1], &f.rs1, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "j") == 0) {
        if (n != 1) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected 'j label'");
        const InstructionHandler *h = isa_find_by_mnemonic("jal");
        Fields f = {0, 0, 0, 0, 0, RM_DYN_DEFAULT};
        int st_ok = parse_value(tok[0], st, addr, 1, &f.imm, err, lineno);
        if (st_ok != ASM_OK) return st_ok;
        memory_write_word(mem, addr, encode_instruction(h, f));
        return ASM_OK;
    }
    if (strcmp(mnemonic, "li") == 0 || strcmp(mnemonic, "la") == 0) {
        if (n != 2) return set_err(err, lineno, ASM_ERR_SYNTAX, "expected '%s rd, value'", mnemonic);
        uint32_t rd;
        if (parse_register(tok[0], &rd, err, lineno) != ASM_OK) return ASM_ERR_BAD_OPERAND;
        int32_t value;
        int st_ok = parse_value(tok[1], st, addr, 0, &value, err, lineno);
        if (st_ok != ASM_OK) return st_ok;

        uint32_t upper20; int32_t lower12;
        li_split(value, &upper20, &lower12);

        const InstructionHandler *hl = isa_find_by_mnemonic("lui");
        const InstructionHandler *ha = isa_find_by_mnemonic("addi");
        Fields fl = {rd, 0, 0, (int32_t)upper20, 0, RM_DYN_DEFAULT};
        Fields fa = {rd, rd, 0, lower12, 0, RM_DYN_DEFAULT};
        memory_write_word(mem, addr,     encode_instruction(hl, fl));
        memory_write_word(mem, addr + 4, encode_instruction(ha, fa));
        return ASM_OK;
    }
    return set_err(err, lineno, ASM_ERR_UNKNOWN_MNEMONIC, "unknown pseudo-instruction '%s'", mnemonic);
}

/* One-for-one aliases: rewrite the line into a real instruction before
 * either pass looks at it. %0 %1 %2 are the alias's operands. */
typedef struct { const char *alias; int nops; const char *mn; const char *ops; } Alias;

static const Alias aliases[] = {
    /* RV32F */
    {"fmv.s",    2, "fsgnj.s",  "%0, %1, %1"},
    {"fneg.s",   2, "fsgnjn.s", "%0, %1, %1"},
    {"fabs.s",   2, "fsgnjx.s", "%0, %1, %1"},
    {"frcsr",    1, "csrrs",    "%0, fcsr, zero"},
    {"fscsr",    1, "csrrw",    "zero, fcsr, %0"},
    {"fscsr",    2, "csrrw",    "%0, fcsr, %1"},
    {"frrm",     1, "csrrs",    "%0, frm, zero"},
    {"fsrm",     1, "csrrw",    "zero, frm, %0"},
    {"fsrm",     2, "csrrw",    "%0, frm, %1"},
    {"fsrmi",    1, "csrrwi",   "zero, frm, %0"},
    {"fsrmi",    2, "csrrwi",   "%0, frm, %1"},
    {"frflags",  1, "csrrs",    "%0, fflags, zero"},
    {"fsflags",  1, "csrrw",    "zero, fflags, %0"},
    {"fsflags",  2, "csrrw",    "%0, fflags, %1"},
    {"fsflagsi", 1, "csrrwi",   "zero, fflags, %0"},
    {"fsflagsi", 2, "csrrwi",   "%0, fflags, %1"},
    /* Zicsr */
    {"csrr",     2, "csrrs",    "%0, %1, zero"},
    {"csrw",     2, "csrrw",    "zero, %0, %1"},
    {"csrs",     2, "csrrs",    "zero, %0, %1"},
    {"csrc",     2, "csrrc",    "zero, %0, %1"},
    {"csrwi",    2, "csrrwi",   "zero, %0, %1"},
    {"csrsi",    2, "csrrsi",   "zero, %0, %1"},
    {"csrci",    2, "csrrci",   "zero, %0, %1"},
    /* RV32I */
    {"beqz",     2, "beq",      "%0, zero, %1"},
    {"bnez",     2, "bne",      "%0, zero, %1"},
    {"blez",     2, "bge",      "zero, %0, %1"},
    {"bgez",     2, "bge",      "%0, zero, %1"},
    {"bltz",     2, "blt",      "%0, zero, %1"},
    {"bgtz",     2, "blt",      "zero, %0, %1"},
    {"bgt",      3, "blt",      "%1, %0, %2"},
    {"ble",      3, "bge",      "%1, %0, %2"},
    {"bgtu",     3, "bltu",     "%1, %0, %2"},
    {"bleu",     3, "bgeu",     "%1, %0, %2"},
    {"neg",      2, "sub",      "%0, zero, %1"},
    {"not",      2, "xori",     "%0, %1, -1"},
    {"seqz",     2, "sltiu",    "%0, %1, 1"},
    {"snez",     2, "sltu",     "%0, zero, %1"},
    {"sltz",     2, "slt",      "%0, %1, zero"},
    {"sgtz",     2, "slt",      "%0, zero, %1"},
    {"jr",       1, "jalr",     "zero, 0(%0)"},
    {"jalr",     1, "jalr",     "ra, 0(%0)"},
    {"jal",      1, "jal",      "ra, %0"},
    {"call",     1, "jal",      "ra, %0"},
    {"tail",     1, "jal",      "zero, %0"},
};

/* Rewrites mnemonic/operands in place if they name an alias. */
static void expand_alias(char *mnemonic, char *operands) {
    char buf[256];
    strncpy(buf, operands, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *tok[4];
    int n = buf[0] ? split_commas(buf, tok, 4) : 0;
    for (unsigned k = 0; k < sizeof(aliases) / sizeof(aliases[0]); k++) {
        const Alias *a = &aliases[k];
        if (a->nops != n || strcmp(a->alias, mnemonic) != 0) continue;
        char out[256] = "";
        for (const char *p = a->ops; *p; p++) {
            if (p[0] == '%' && p[1] >= '0' && p[1] <= '2') {
                strncat(out, tok[p[1] - '0'], sizeof(out) - strlen(out) - 1);
                p++;
            } else {
                size_t len = strlen(out);
                if (len + 1 < sizeof(out)) { out[len] = *p; out[len + 1] = '\0'; }
            }
        }
        strcpy(mnemonic, a->mn);
        strcpy(operands, out);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Directives (.word / .half / .byte / .align / .text / .data / .globl)*/
/* ------------------------------------------------------------------ */

static int directive_element_size(const char *mnemonic) {
    if (strcmp(mnemonic, ".word") == 0) return 4;
    if (strcmp(mnemonic, ".half") == 0) return 2;
    if (strcmp(mnemonic, ".byte") == 0) return 1;
    if (strcmp(mnemonic, ".float") == 0) return 4;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Error helper                                                       */
/* ------------------------------------------------------------------ */

#include <stdarg.h>
static int set_err(AsmError *err, int lineno, AsmStatus status, const char *fmt, ...) {
    if (err) {
        err->line_number = lineno;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->message, sizeof(err->message), fmt, ap);
        va_end(ap);
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Main two-pass driver                                               */
/* ------------------------------------------------------------------ */

static int read_lines(const char *filename, SrcLine *lines, int *out_count, AsmError *err) {
    FILE *f = fopen(filename, "r");
    if (!f) return set_err(err, 0, ASM_ERR_IO, "could not open '%s'", filename);

    int count = 0;
    char raw[MAX_LINE_LEN];
    int lineno = 0;
    while (fgets(raw, sizeof(raw), f)) {
        lineno++;
        if (count >= MAX_LINES) {
            fclose(f);
            return set_err(err, lineno, ASM_ERR_TOO_MANY_LINES, "source file exceeds %d lines", MAX_LINES);
        }
        strncpy(lines[count].text, raw, MAX_LINE_LEN - 1);
        lines[count].text[MAX_LINE_LEN - 1] = '\0';
        lines[count].lineno = lineno;
        count++;
    }
    fclose(f);
    *out_count = count;
    return ASM_OK;
}

AsmStatus assemble_file(const char *filename, Memory *mem, uint32_t *out_entry_point, AsmError *err) {
    static SrcLine lines[MAX_LINES]; /* static: too large for the stack */
    static SymbolTable st;
    int nlines = 0;
    st.count = 0;

    int rc = read_lines(filename, lines, &nlines, err);
    if (rc != ASM_OK) return (AsmStatus)rc;

    /* ---------------- pass 1: build the symbol table ---------------- */
    uint32_t addr = 0;
    for (int i = 0; i < nlines; i++) {
        char work[MAX_LINE_LEN];
        strncpy(work, lines[i].text, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';

        char label[64], mnemonic[32], operands[256];
        tokenize_line(work, label, mnemonic, operands);
        expand_alias(mnemonic, operands);

        if (label[0]) {
            if (symtab_add(&st, label, addr) != 0) {
                return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_DUPLICATE_SYMBOL,
                                           "duplicate label '%s'", label);
            }
        }
        if (!mnemonic[0]) continue;

        if (strcmp(mnemonic, ".align") == 0) {
            char *endptr; long n = strtol(operands, &endptr, 0);
            if (endptr == operands || n <= 0) {
                return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_SYNTAX, ".align expects a positive integer");
            }
            if (addr % (uint32_t)n != 0) addr += (uint32_t)n - (addr % (uint32_t)n);
            continue;
        }
        if (is_pseudo_directive(mnemonic)) continue;

        int elem_size = directive_element_size(mnemonic);
        if (elem_size > 0) {
            char buf[256]; strncpy(buf, operands, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            char *tok[64];
            int n = split_commas(buf, tok, 64);
            addr += (uint32_t)(elem_size * n);
            continue;
        }

        int pseudo_size = pseudo_instruction_size(mnemonic);
        if (pseudo_size > 0) { addr += (uint32_t)pseudo_size; continue; }

        if (isa_find_by_mnemonic(mnemonic)) { addr += 4; continue; }

        return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_UNKNOWN_MNEMONIC,
                                   "unknown instruction or directive '%s'", mnemonic);
    }

    /* ---------------- pass 2: encode ---------------- */
    addr = 0;
    for (int i = 0; i < nlines; i++) {
        char work[MAX_LINE_LEN];
        strncpy(work, lines[i].text, sizeof(work) - 1); work[sizeof(work) - 1] = '\0';

        char label[64], mnemonic[32], operands[256];
        tokenize_line(work, label, mnemonic, operands);
        expand_alias(mnemonic, operands);
        (void)label; /* already recorded in pass 1 */

        if (!mnemonic[0]) continue;

        if (strcmp(mnemonic, ".align") == 0) {
            long n = strtol(operands, NULL, 0);
            if (addr % (uint32_t)n != 0) addr += (uint32_t)n - (addr % (uint32_t)n);
            continue;
        }
        if (is_pseudo_directive(mnemonic)) continue;

        int elem_size = directive_element_size(mnemonic);
        if (elem_size > 0) {
            char buf[256]; strncpy(buf, operands, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            char *tok[64];
            int n = split_commas(buf, tok, 64);
            for (int k = 0; k < n; k++) {
                int32_t v;
                if (strcmp(mnemonic, ".float") == 0) {
                    char *end;
                    float fv = strtof(tok[k], &end);
                    if (end == tok[k] || *end != '\0')
                        return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_BAD_OPERAND, "bad .float value '%s'", tok[k]);
                    memcpy(&v, &fv, sizeof v);
                } else {
                    int st_ok = parse_value(tok[k], &st, addr, 0, &v, err, lines[i].lineno);
                    if (st_ok != ASM_OK) return (AsmStatus)st_ok;
                }
                MemStatus ms;
                if (elem_size == 4) ms = memory_write_word(mem, addr, (uint32_t)v);
                else if (elem_size == 2) ms = memory_write_half(mem, addr, (uint16_t)v);
                else ms = memory_write_byte(mem, addr, (uint8_t)v);
                if (ms != MEM_OK) return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_MEMORY,
                                                             "data directive writes past end of memory");
                addr += (uint32_t)elem_size;
            }
            continue;
        }

        int pseudo_size = pseudo_instruction_size(mnemonic);
        if (pseudo_size > 0) {
            int st_ok = assemble_pseudo(mnemonic, operands, &st, addr, mem, err, lines[i].lineno);
            if (st_ok != ASM_OK) return (AsmStatus)st_ok;
            addr += (uint32_t)pseudo_size;
            continue;
        }

        const InstructionHandler *h = isa_find_by_mnemonic(mnemonic);
        if (!h) {
            return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_UNKNOWN_MNEMONIC,
                                       "unknown instruction or directive '%s'", mnemonic);
        }
        Fields f;
        int st_ok = parse_fields(h, operands, &st, addr, &f, err, lines[i].lineno);
        if (st_ok != ASM_OK) return (AsmStatus)st_ok;

        uint32_t word = encode_instruction(h, f);
        if (memory_write_word(mem, addr, word) != MEM_OK) {
            return (AsmStatus)set_err(err, lines[i].lineno, ASM_ERR_MEMORY, "program too large for memory");
        }
        addr += 4;
    }

    int main_idx = symtab_find(&st, "main");
    *out_entry_point = (main_idx >= 0) ? st.items[main_idx].address : 0;
    return ASM_OK;
}
