#!/usr/bin/env python3
"""Random-program differential tester.

Generates valid, always-terminating RISC-V programs and runs each one
through a lab's compare.sh (pipeline in every mode vs the single-cycle
oracle). Usage:

    python3 tests/fuzz.py <labdir> <count> [first_seed] [--fp]

--fp adds the M extension, RV32F and the FP CSRs (lab5 only). Failing
programs are kept in tests/fuzz_fail/ for replay; passing ones are deleted.
"""
import os, random, subprocess, sys

POOL = ["t0", "t1", "t2", "t3", "t4", "t5", "t6", "s1", "s2", "s3", "s4",
        "s5", "s6", "s7", "a0", "a1", "a2", "a3", "a4", "a5", "a6"]
# Reserved: s0 = data base, s8/s9 = loop counters, s10 = indirect target,
# a7 = syscall number, ra/sp untouched by random code.
R_OPS = ["add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and"]
I_OPS = ["addi", "slti", "sltiu", "xori", "ori", "andi"]
SH_OPS = ["slli", "srli", "srai"]
BR_OPS = ["beq", "bne", "blt", "bge", "bltu", "bgeu",
          "beqz", "bnez", "bgt", "ble", "bgtu", "bleu"]
M_OPS = ["mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu"]
LOADS = [("lb", 1), ("lh", 2), ("lw", 4), ("lbu", 1), ("lhu", 2)]
STORES = [("sb", 1), ("sh", 2), ("sw", 4)]
DATA_BYTES = 256
FP_SPECIALS = [0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x40490fdb,
               0x7f800000, 0xff800000, 0x7fc00000, 0x7fa00000, 0x7f7fffff,
               0x00800000, 0x00000001, 0x807fffff, 0x3eaaaaab, 0x4b800001,
               0xcf000000, 0x4f800000, 0x40200000, 0xc0200000, 0x3fc00000]
FREGS = ["f%d" % i for i in range(16)]
RMS = ["", "", "", ", rne", ", rtz", ", rdn", ", rup", ", rmm", ", dyn"]


class Gen:
    def __init__(self, seed, fp):
        self.r = random.Random(seed)
        self.fp = fp
        self.lines = []
        self.funcs = []
        self.label_n = 0
        self.recent = []        # registers named lately: reuse them to make dependences
        self.frecent = []

    def pick(self, pool, recent):
        if recent and self.r.random() < 0.4:
            x = self.r.choice(recent)
        else:
            x = self.r.choice(pool)
        recent.append(x)
        del recent[:-3]
        return x

    def reg(self, allow_zero=True):
        if allow_zero and self.r.random() < 0.05:
            return "zero"
        return self.pick(POOL, self.recent)

    def label(self):
        self.label_n += 1
        return "L%d" % self.label_n

    def emit(self, s):
        self.lines.append("    " + s)

    # ---- one random straight-line instruction
    def simple(self):
        r = self.r
        k = r.random()
        if k < 0.25:
            self.emit("%s %s, %s, %s" % (r.choice(R_OPS), self.reg(), self.reg(), self.reg()))
        elif k < 0.40:
            self.emit("%s %s, %s, %d" % (r.choice(I_OPS), self.reg(), self.reg(), r.randint(-2048, 2047)))
        elif k < 0.47:
            self.emit("%s %s, %s, %d" % (r.choice(SH_OPS), self.reg(), self.reg(), r.randint(0, 31)))
        elif k < 0.52:
            self.emit("lui %s, %d" % (self.reg(), r.randint(0, 0xFFFFF)))
        elif k < 0.55:
            self.emit("auipc %s, %d" % (self.reg(), r.randint(0, 0xFF)))
        elif k < 0.68:
            op, sz = r.choice(LOADS)
            rd = self.reg(False)
            self.emit("%s %s, %d(s0)" % (op, rd, sz * r.randrange(DATA_BYTES // sz)))
            if r.random() < 0.5:            # load-use, as an ALU source or as store data
                if r.random() < 0.7:
                    self.emit("%s %s, %s, %s" % (r.choice(R_OPS), self.reg(), rd, self.reg()))
                else:
                    op, sz = r.choice(STORES)
                    self.emit("%s %s, %d(s0)" % (op, rd, sz * r.randrange(DATA_BYTES // sz)))
        elif k < 0.78:
            op, sz = r.choice(STORES)
            self.emit("%s %s, %d(s0)" % (op, self.reg(), sz * r.randrange(DATA_BYTES // sz)))
        elif k < 0.82:
            self.emit(r.choice(["neg", "not", "seqz", "snez", "sltz", "sgtz", "mv"])
                      + " %s, %s" % (self.reg(), self.reg()))
        elif self.fp and k < 0.86:
            self.emit("%s %s, %s, %s" % (r.choice(M_OPS), self.reg(), self.reg(), self.reg()))
        elif self.fp:
            self.fp_op()
        else:
            self.emit("addi %s, %s, %d" % (self.reg(), self.reg(), r.randint(-50, 50)))

    def fp_op(self):
        r = self.r
        f = lambda: self.pick(FREGS, self.frecent)
        rm = r.choice(RMS)
        k = r.random()
        if k < 0.12:
            fd = f()
            self.emit("flw %s, %d(s0)" % (fd, DATA_BYTES + 4 * r.randrange(len(FP_SPECIALS))))
            if r.random() < 0.5:            # FP load-use, possibly as the FMA third operand
                if r.random() < 0.5:
                    self.emit("fmadd.s %s, %s, %s, %s" % (f(), f(), f(), fd))
                else:
                    self.emit("fmul.s %s, %s, %s" % (f(), fd, f()))
        elif k < 0.18:
            self.emit("flw %s, %d(s0)" % (f(), 4 * r.randrange(DATA_BYTES // 4)))
        elif k < 0.24:
            self.emit("fsw %s, %d(s0)" % (f(), 4 * r.randrange(DATA_BYTES // 4)))
        elif k < 0.42:
            op = r.choice(["fadd.s", "fsub.s", "fmul.s", "fdiv.s"])
            self.emit("%s %s, %s, %s%s" % (op, f(), f(), f(), rm))
        elif k < 0.46:
            self.emit("fsqrt.s %s, %s%s" % (f(), f(), rm))
        elif k < 0.54:
            op = r.choice(["fmadd.s", "fmsub.s", "fnmsub.s", "fnmadd.s"])
            self.emit("%s %s, %s, %s, %s%s" % (op, f(), f(), f(), f(), rm))
        elif k < 0.62:
            op = r.choice(["fsgnj.s", "fsgnjn.s", "fsgnjx.s", "fmin.s", "fmax.s"])
            self.emit("%s %s, %s, %s" % (op, f(), f(), f()))
        elif k < 0.66:
            self.emit("%s %s, %s" % (r.choice(["fmv.s", "fneg.s", "fabs.s"]), f(), f()))
        elif k < 0.72:
            op = r.choice(["feq.s", "flt.s", "fle.s"])
            self.emit("%s %s, %s, %s" % (op, self.reg(), f(), f()))
        elif k < 0.80:
            op = r.choice(["fcvt.w.s", "fcvt.wu.s"])
            self.emit("%s %s, %s%s" % (op, self.reg(), f(), rm))
        elif k < 0.86:
            op = r.choice(["fcvt.s.w", "fcvt.s.wu"])
            self.emit("%s %s, %s%s" % (op, f(), self.reg(), rm))
        elif k < 0.90:
            op = r.choice(["fmv.x.w", "fclass.s"])
            self.emit("%s %s, %s" % (op, self.reg(), f()))
        elif k < 0.93:
            self.emit("fmv.w.x %s, %s" % (f(), self.reg()))
        else:
            c = r.random()
            if c < 0.3:   self.emit("frflags %s" % self.reg())
            elif c < 0.5: self.emit("fsflags %s" % r.choice(["zero", self.reg()]))
            elif c < 0.7: self.emit("fsrmi %d" % r.randint(0, 4))   # only legal modes
            elif c < 0.85: self.emit("frrm %s" % self.reg())
            else:         self.emit("frcsr %s" % self.reg())

    def straight(self, lo, hi):
        for _ in range(self.r.randint(lo, hi)):
            self.simple()

    # ---- control-flow blocks
    def block(self, depth):
        r = self.r
        k = r.random()
        if k < 0.45:
            self.straight(1, 4)
        elif k < 0.65:                      # forward branch over a few instructions
            l = self.label()
            op = r.choice(BR_OPS)
            if op in ("beqz", "bnez"):
                self.emit("%s %s, %s" % (op, self.reg(), l))
            else:
                self.emit("%s %s, %s, %s" % (op, self.reg(), self.reg(), l))
            self.straight(1, 3)
            self.lines.append(l + ":")
        elif k < 0.78 and depth < 2:        # counted loop (s8 outer, s9 inner)
            ctr = "s8" if depth == 0 else "s9"
            l = self.label()
            self.emit("li %s, %d" % (ctr, r.randint(1, 5)))
            self.lines.append(l + ":")
            for _ in range(r.randint(1, 3)):
                self.block(depth + 1)
            self.emit("addi %s, %s, -1" % (ctr, ctr))
            self.emit("bnez %s, %s" % (ctr, l))
        elif k < 0.88:                      # direct or indirect call
            fn = "F%d" % len(self.funcs)
            body = []
            for _ in range(r.randint(1, 4)):
                save, self.lines = self.lines, []
                self.simple()
                body += self.lines
                self.lines = save
            self.funcs.append((fn, body))
            if r.random() < 0.5:
                self.emit(r.choice(["call", "jal"]) + " " + fn)
            else:
                self.emit("la s10, %s" % fn)
                self.emit("jalr s10")
        else:                               # print a value
            self.emit("mv a0, %s" % self.reg())
            self.emit("addi a7, zero, 1")
            self.emit("ecall")

    def program(self):
        r = self.r
        self.lines += [".text", ".globl main", "main:"]
        self.emit("la s0, data")
        for reg in r.sample(POOL, 6):
            self.emit("li %s, %d" % (reg, r.randint(-2**31, 2**31 - 1)))
        if self.fp:
            for i in range(8):
                self.emit("flw f%d, %d(s0)" % (i, DATA_BYTES + 4 * r.randrange(len(FP_SPECIALS))))
        for _ in range(r.randint(8, 25)):
            self.block(0)
        self.emit("addi a7, zero, 10")
        self.emit("ecall")
        for fn, body in self.funcs:
            self.lines.append(fn + ":")
            self.lines += body
            self.emit("ret")
        self.lines += [".data", "data:"]
        words = [r.randint(0, 2**32 - 1) for _ in range(DATA_BYTES // 4)]
        if self.fp:
            words += FP_SPECIALS
        for i in range(0, len(words), 8):     # the assembler keeps 255 operand chars
            self.lines.append("    .word " + ", ".join("0x%08x" % w for w in words[i:i + 8]))
        return "\n".join(self.lines) + "\n"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    fp = "--fp" in sys.argv
    lab, count = args[0], int(args[1])
    first = int(args[2]) if len(args) > 2 else 1
    here = os.path.dirname(os.path.abspath(__file__))
    faildir = os.path.join(here, "fuzz_fail")
    os.makedirs(faildir, exist_ok=True)
    runs = fails = 0
    for seed in range(first, first + count):
        path = os.path.join(faildir, "%s_seed%d.s" % (os.path.basename(lab.rstrip("/")), seed))
        with open(path, "w") as fh:
            fh.write(Gen(seed, fp).program())
        out = subprocess.run(["./compare.sh", path], cwd=lab, capture_output=True, text=True).stdout
        lines = [l for l in out.splitlines() if l.strip()]
        bad = [l for l in lines if " MATCH " not in l or "FAIL" in l]
        runs += len(lines)
        if bad or not lines:
            fails += 1
            print("seed %d FAILED:\n  %s" % (seed, "\n  ".join(bad or ["(no output)"])))
        else:
            os.remove(path)
    print("%s: %d programs, %d runs, %d failing programs" % (lab, count, runs, fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
