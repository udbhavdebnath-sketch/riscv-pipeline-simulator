#!/usr/bin/env python3
"""Independent check of RV32I + M instruction semantics.

Random operands through every ALU, shift, compare, branch-condition, M and
load/store-width instruction; results are compared against Python's own
arithmetic, not against the simulator.

    python3 tests/intref.py <lab dir> <cases> [seed] [--m]
"""
import random, re, subprocess, sys, tempfile, os

M32 = 0xFFFFFFFF
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v

def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v

def trunc_div(a, b):            # C-style division, toward zero
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

R = {
    "add":  lambda a, b: a + b,          "sub":  lambda a, b: a - b,
    "sll":  lambda a, b: a << (b & 31),  "srl":  lambda a, b: a >> (b & 31),
    "sra":  lambda a, b: s32(a) >> (b & 31),
    "slt":  lambda a, b: int(s32(a) < s32(b)), "sltu": lambda a, b: int(a < b),
    "xor":  lambda a, b: a ^ b, "or": lambda a, b: a | b, "and": lambda a, b: a & b,
}
M = {
    "mul":    lambda a, b: a * b,
    "mulh":   lambda a, b: (s32(a) * s32(b)) >> 32,
    "mulhsu": lambda a, b: (s32(a) * b) >> 32,
    "mulhu":  lambda a, b: (a * b) >> 32,
    "div":    lambda a, b: -1 if b == 0 else (s32(a) if (s32(a), s32(b)) == (-2**31, -1) else trunc_div(s32(a), s32(b))),
    "divu":   lambda a, b: M32 if b == 0 else a // b,
    "rem":    lambda a, b: s32(a) if b == 0 else (0 if (s32(a), s32(b)) == (-2**31, -1) else s32(a) - trunc_div(s32(a), s32(b)) * s32(b)),
    "remu":   lambda a, b: a if b == 0 else a % b,
}
I = {
    "addi": lambda a, i: a + i, "slti": lambda a, i: int(s32(a) < i), "sltiu": lambda a, i: int(a < (i & M32)),
    "xori": lambda a, i: a ^ (i & M32), "ori": lambda a, i: a | (i & M32), "andi": lambda a, i: a & (i & M32),
}
SH = {"slli": lambda a, s: a << s, "srli": lambda a, s: a >> s, "srai": lambda a, s: s32(a) >> s}
BR = {"beq": lambda a, b: a == b, "bne": lambda a, b: a != b, "blt": lambda a, b: s32(a) < s32(b),
      "bge": lambda a, b: s32(a) >= s32(b), "bltu": lambda a, b: a < b, "bgeu": lambda a, b: a >= b}
LD = {"lb": lambda w, o: sext(w >> (8 * o), 8), "lbu": lambda w, o: (w >> (8 * o)) & 0xFF,
      "lh": lambda w, o: sext(w >> (8 * o), 16), "lhu": lambda w, o: (w >> (8 * o)) & 0xFFFF, "lw": lambda w, o: w}
ST = {"sb": (1, 0xFF), "sh": (2, 0xFFFF), "sw": (4, M32)}

def rand_word(r):
    return r.choice([0, 1, 2, 31, 32, 0x7FFFFFFF, 0x80000000, M32, 0xFFFFFFFE, 0x8000, 0xFFFF8000,
                     r.getrandbits(32), r.getrandbits(32), r.getrandbits(8), r.getrandbits(16)])

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    lab, n = args[0], int(args[1])
    r = random.Random(int(args[2]) if len(args) > 2 else 1)
    kinds = ["R", "I", "SH", "BR", "LD", "ST", "LUI"] + (["M", "M"] if "--m" in sys.argv else [])
    cases, L = [], [".text", "main:", "    la s0, ops", "    la s1, out", "    la s2, scratch", "    mv s3, s1"]
    for k in range(n):
        kind, a, b = r.choice(kinds), rand_word(r), rand_word(r)
        L += ["    lw t0, 0(s0)", "    lw t1, 4(s0)"]
        if kind in ("R", "M"):
            op = r.choice(list(R if kind == "R" else M))
            L.append("    %s t2, t0, t1" % op); exp = (R if kind == "R" else M)[op](a, b)
        elif kind == "I":
            op, imm = r.choice(list(I)), r.randint(-2048, 2047)
            L.append("    %s t2, t0, %d" % (op, imm)); exp = I[op](a, imm)
        elif kind == "SH":
            op, sh = r.choice(list(SH)), r.randint(0, 31)
            L.append("    %s t2, t0, %d" % (op, sh)); exp = SH[op](a, sh)
        elif kind == "BR":
            op, lbl = r.choice(list(BR)), "T%d" % k
            L += ["    li t2, 0", "    %s t0, t1, %s" % (op, lbl), "    li t2, 1", "%s:" % lbl]
            exp = 0 if BR[op](a, b) else 1
        elif kind == "LUI":
            imm = r.getrandbits(20)
            L.append("    lui t2, %d" % imm); exp = imm << 12
        elif kind == "LD":         # store a known word, read part of it back
            op = r.choice(list(LD))
            off = {"lw": 0, "lh": r.choice([0, 2]), "lhu": r.choice([0, 2])}.get(op, r.randint(0, 3))
            L += ["    sw t0, 0(s2)", "    %s t2, %d(s2)" % (op, off)]; exp = LD[op](a, off)
        else:                      # store part of a register over a known word, read the word
            op = r.choice(list(ST)); size, mask = ST[op]
            off = r.randrange(0, 4, size)
            L += ["    sw t1, 0(s2)", "    %s t0, %d(s2)" % (op, off), "    lw t2, 0(s2)"]
            exp = (b & ~(mask << (8 * off)) | ((a & mask) << (8 * off))) & M32
        L += ["    sw t2, 0(s1)", "    addi s0, s0, 8", "    addi s1, s1, 4"]
        cases.append((kind, L[-4 if kind not in ("BR", "LD", "ST") else -6:-3], a, b, exp & M32))
    L += ["    addi a7, zero, 10", "    ecall", ".data", "scratch: .word 0", "ops:"]
    L += ["    .word 0x%08x, 0x%08x" % (c[2], c[3]) for c in cases] + ["out: .word 0"]
    with tempfile.NamedTemporaryFile("w", suffix=".s", delete=False) as fh:
        fh.write("\n".join(L) + "\n")
        path = fh.name
    binary = os.path.join(lab, "rvsim")
    flags = ["--batch"] + ([] if lab.rstrip("/").endswith("lab2") else ["--single-cycle"])
    regs = subprocess.run([binary, path] + flags, capture_output=True, text=True).stdout
    m = re.search(r"x19/s3  =0x([0-9a-f]+)", regs)
    if not m:
        sys.exit("simulator did not run %s:\n%s" % (path, regs[-500:]))
    dump = subprocess.run([binary, path], input="run\nmem %d %d\nquit\n" % (int(m.group(1), 16), 4 * n),
                          capture_output=True, text=True).stdout
    by = [int(x, 16) for line in dump.splitlines() if re.match(r"0x[0-9a-f]+: ", line)
          for x in line.split(":", 1)[1].split()]
    got = [int.from_bytes(bytes(by[i:i + 4]), "little") for i in range(0, len(by), 4)]
    bad = 0
    for k, (kind, text, a, b, exp) in enumerate(cases):
        if got[k] != exp:
            bad += 1
            if bad <= 20:
                print("MISMATCH %s a=%08x b=%08x got %08x want %08x   %s" % (kind, a, b, got[k], exp, " / ".join(t.strip() for t in text)))
    os.unlink(path)
    print("intref %s: %d cases, %d mismatches" % (lab, n, bad))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
