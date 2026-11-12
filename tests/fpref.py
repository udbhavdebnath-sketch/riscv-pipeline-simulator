#!/usr/bin/env python3
"""Independent RV32F reference, in exact rational arithmetic.

Rounds exact results to binary32 by the RISC-V rules (five modes,
tininess detected after rounding, canonical NaN), generates random cases,
runs them through a lab5 binary and compares result bits and fflags.

    python3 tests/fpref.py <lab5 dir> <cases> [seed]
"""
import math, random, re, subprocess, sys, tempfile, os
from fractions import Fraction as Q

NV, DZ, OF, UF, NX = 0x10, 0x08, 0x04, 0x02, 0x01
CNAN = 0x7FC00000
RNE, RTZ, RDN, RUP, RMM = range(5)
MAXF = Q(2**24 - 1, 2**23) * Q(2**127)
MINN = Q(1, 2**126)

def is_nan(b):  return (b & 0x7F800000) == 0x7F800000 and (b & 0x7FFFFF) != 0
def is_snan(b): return is_nan(b) and not (b & 0x400000)
def is_inf(b):  return (b & 0x7FFFFFFF) == 0x7F800000
def sign(b):    return b >> 31

def value(b):
    """Exact value of a finite float (a Fraction; zeros lose their sign)."""
    e, m = (b >> 23) & 0xFF, b & 0x7FFFFF
    v = Q(m, 2**149) if e == 0 else Q(m + 2**23, 2**23) * Q(2) ** (e - 127)
    return -v if sign(b) else v

def encode(neg, a):
    """Bits of the float exactly equal to |a| = a (already representable)."""
    if a == 0: return neg << 31
    if a >= Q(2)**128: return (neg << 31) | 0x7F800000
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Q(2) ** e > a: e -= 1
    if e < -126:
        m = a * 2**149
        assert m.denominator == 1
        return (neg << 31) | int(m)
    m = a / Q(2) ** e * 2**23
    assert m.denominator == 1
    return (neg << 31) | ((e + 127) << 23) | (int(m) - 2**23)

def round_mag(a, neg, rm, quantum):
    """Round |a| to a multiple of quantum; returns (rounded, inexact)."""
    m = a / quantum
    n = m.numerator // m.denominator
    rem = m - n
    if rem:
        up = {RNE: rem > Q(1, 2) or (rem == Q(1, 2) and n % 2 == 1),
              RTZ: False, RDN: neg, RUP: not neg, RMM: rem >= Q(1, 2)}[rm]
        n += up
    return n * quantum, rem != 0

def quantum_of(a, unbounded):
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Q(2) ** e > a: e -= 1
    if not unbounded and e < -126: e = -126
    return Q(2) ** (e - 23)

def round_f32(q, rm, zero_sign=0):
    """Exact rational -> (bits, flags) by RISC-V rules."""
    if q == 0: return zero_sign << 31, 0
    neg, a = int(q < 0), abs(q)
    v, inexact = round_mag(a, neg, rm, quantum_of(a, False))
    flags = NX if inexact else 0
    if v > MAXF:
        to_inf = rm in (RNE, RMM) or (rm == RUP and not neg) or (rm == RDN and neg)
        return (neg << 31) | (0x7F800000 if to_inf else 0x7F7FFFFF), OF | NX
    vu, _ = round_mag(a, neg, rm, quantum_of(a, True))   # tininess after rounding
    if vu < MINN and inexact: flags |= UF
    return encode(neg, v), flags

def nan_case(*ops):
    flags = NV if any(is_snan(x) for x in ops) else 0
    return CNAN, flags

# ---------------- arithmetic

def f_add(A, B, rm, sub=False):
    if sub: B ^= 0x80000000
    if is_nan(A) or is_nan(B): return nan_case(A, B)
    if is_inf(A) or is_inf(B):
        if is_inf(A) and is_inf(B) and sign(A) != sign(B): return CNAN, NV
        return (A if is_inf(A) else B), 0
    q = value(A) + value(B)
    if q == 0:
        # IEEE 754 6.3: (+0)+(-0) and x+(-x) are +0, except -0 when rounding down;
        # two zeros of the same sign keep it.
        if value(A) == 0 and value(B) == 0:
            zs = (sign(A) & sign(B)) if rm != RDN else (sign(A) | sign(B))
        else:
            zs = int(rm == RDN)
        return zs << 31, 0
    return round_f32(q, rm)

def f_mul(A, B, rm):
    if is_nan(A) or is_nan(B): return nan_case(A, B)
    s = sign(A) ^ sign(B)
    if is_inf(A) or is_inf(B):
        if (not is_inf(A) and value(A) == 0) or (not is_inf(B) and value(B) == 0): return CNAN, NV
        return (s << 31) | 0x7F800000, 0
    return round_f32(value(A) * value(B), rm, s)

def f_div(A, B, rm):
    if is_nan(A) or is_nan(B): return nan_case(A, B)
    s = sign(A) ^ sign(B)
    if is_inf(A) and is_inf(B): return CNAN, NV
    if is_inf(A): return (s << 31) | 0x7F800000, 0
    if is_inf(B): return s << 31, 0
    if value(B) == 0:
        if value(A) == 0: return CNAN, NV
        return (s << 31) | 0x7F800000, DZ
    return round_f32(value(A) / value(B), rm, s)

def f_sqrt(A, rm):
    if is_nan(A): return nan_case(A)
    if A == 0x80000000: return A, 0
    if sign(A): return CNAN, NV
    if is_inf(A): return A, 0
    a = value(A)
    if a == 0: return 0, 0
    k = 200
    N = a * Q(2) ** (2 * k)
    n = N.numerator // N.denominator
    r = math.isqrt(n)
    exact = (r * r == n) and N.denominator == 1
    q = Q(r, 2**k) + (0 if exact else Q(1, 2**(k + 2)))   # strictly between when inexact
    return round_f32(q, rm)

def f_fma(A, B, C, rm, negprod=False, negadd=False):
    if negprod: A ^= 0x80000000
    if negadd:  C ^= 0x80000000
    inf_times_zero = (is_inf(A) and not is_nan(B) and not is_inf(B) and value(B) == 0) or \
                     (is_inf(B) and not is_nan(A) and not is_inf(A) and value(A) == 0)
    if inf_times_zero: return CNAN, NV            # RISC-V: even with a quiet-NaN addend
    if is_nan(A) or is_nan(B) or is_nan(C): return nan_case(A, B, C)
    ps = sign(A) ^ sign(B)
    if is_inf(A) or is_inf(B):
        if is_inf(C) and sign(C) != ps: return CNAN, NV
        return (ps << 31) | 0x7F800000, 0
    if is_inf(C): return C, 0
    p = value(A) * value(B)
    q = p + value(C)
    if q == 0:
        if p == 0 and value(C) == 0:
            zs = (ps & sign(C)) if rm != RDN else (ps | sign(C))
        else:
            zs = int(rm == RDN)
        return zs << 31, 0
    return round_f32(q, rm)

def round_int(v, rm):
    n = v.numerator // v.denominator
    rem = v - n
    if rem == 0: return n
    return n + {RNE: rem > Q(1, 2) or (rem == Q(1, 2) and n % 2 == 1),
                RTZ: v < 0, RDN: False, RUP: True, RMM: rem > Q(1, 2) or (rem == Q(1, 2) and v > 0)}[rm]

def f_cvt_to_int(A, rm, unsigned):
    lo, hi = (0, 2**32 - 1) if unsigned else (-2**31, 2**31 - 1)
    if is_nan(A): return hi & 0xFFFFFFFF, NV
    if is_inf(A): return (lo if sign(A) else hi) & 0xFFFFFFFF, NV
    v = value(A)
    n = round_int(v, rm)
    if n > hi: return hi & 0xFFFFFFFF, NV
    if n < lo: return lo & 0xFFFFFFFF, NV
    return n & 0xFFFFFFFF, (NX if n != v else 0)

def f_cvt_from_int(X, rm, unsigned):
    v = X if unsigned else (X - 2**32 if X >> 31 else X)
    return round_f32(Q(v), rm)

def f_cmp(A, B, kind):
    if kind == "feq":
        flags = NV if is_snan(A) or is_snan(B) else 0
        if is_nan(A) or is_nan(B): return 0, flags
        return int(value(A) == value(B)) if not (is_inf(A) or is_inf(B)) else int(A == B or (is_inf(A) and A == B)), flags
    if is_nan(A) or is_nan(B): return 0, NV
    key = lambda b: (Q(-1) * 2**200 if sign(b) else Q(2)**200) if is_inf(b) else value(b)
    return int(key(A) < key(B) if kind == "flt" else key(A) <= key(B)), 0

def f_minmax(A, B, is_max):
    flags = NV if is_snan(A) or is_snan(B) else 0
    if is_nan(A) and is_nan(B): return CNAN, flags
    if is_nan(A): return B, flags
    if is_nan(B): return A, flags
    key = lambda b: ((Q(-1) * 2**200 if sign(b) else Q(2)**200) if is_inf(b) else value(b), -sign(b))
    a, b = key(A), key(B)
    return (A if (a > b) == is_max else B) if a != b else A, flags

def f_class(A):
    s, e, m = sign(A), (A >> 23) & 0xFF, A & 0x7FFFFF
    if e == 0xFF: bit = (9 if m & 0x400000 else 8) if m else (0 if s else 7)
    elif e == 0:  bit = (2 if s else 5) if m else (3 if s else 4)
    else:         bit = 1 if s else 6
    return 1 << bit, 0

# ---------------- case generation

SPECIAL = [0, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00000,
           0xFFC00000, 0x7FA00000, 0x7F800001, 0x7F7FFFFF, 0xFF7FFFFF, 0x00800000, 0x80800000,
           0x007FFFFF, 0x807FFFFF, 0x00000001, 0x80000001, 0x00400000, 0x33800000, 0x4B800000,
           0x4B7FFFFF, 0x3F000000, 0x40000000, 0x4F000000, 0xCF000000, 0x4F800000, 0x3FC00000,
           0x40200000, 0xC0200000, 0x40600000, 0x1F800000, 0x5F800000, 0x00800001, 0x3F800001]

def rand_float(r):
    k = r.random()
    if k < 0.3: return r.choice(SPECIAL)
    if k < 0.45: return r.getrandbits(32)
    if k < 0.6:  # tiny: near the subnormal / normal boundary
        return (r.getrandbits(1) << 31) | r.randint(0, 0x01800000)
    if k < 0.7:  # huge
        return (r.getrandbits(1) << 31) | r.randint(0x7E800000, 0x7F7FFFFF)
    # moderate exponent, random mantissa: lots of inexact results and ties
    return (r.getrandbits(1) << 31) | (r.randint(110, 145) << 23) | r.getrandbits(23)

def rand_int(r):
    return r.choice([0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 16777217, 16777216, 33554435,
                     r.getrandbits(32), r.getrandbits(25), r.getrandbits(32)])

def bits(f):
    """Bits of an exactly representable rational (used to build targeted cases)."""
    return encode(int(f < 0), abs(f))

def targeted(r):
    """Cases aimed at the hard spots: exact ties, the FLT_MIN boundary, exact cancellation."""
    rm = r.randrange(5)
    k = r.random()
    sgn = lambda: r.choice([1, -1])
    if k < 0.25:    # add: A plus exactly half an ulp of A -> a tie
        e = r.randint(-120, 100)
        A = Q(r.randint(2**23, 2**24 - 1), 2**23) * Q(2) ** e * sgn()
        B = Q(2) ** (e - 24) * r.choice([1, -1, 3, -3])
        return ("fadd.s", bits(A), bits(B), 0, 0, rm)
    if k < 0.45:    # mul: two odd 13-bit integers whose product needs 25 bits -> a tie
        while True:
            p, q = r.randrange(2**12 + 1, 2**13, 2), r.randrange(2**12 + 1, 2**13, 2)
            if 2**24 <= p * q < 2**25: break
        e1, e2 = r.randint(-60, 40), r.randint(-60, 40)
        return ("fmul.s", bits(Q(p) * Q(2) ** e1 * sgn()), bits(Q(q) * Q(2) ** e2 * sgn()), 0, 0, rm)
    if k < 0.70:    # mul / div / fma landing right at FLT_MIN
        e1 = r.randint(-75, -50)
        A = Q(r.randint(2**23, 2**24 - 1), 2**23) * Q(2) ** e1 * sgn()
        B = Q(r.randint(2**23, 2**24 - 1), 2**23) * Q(2) ** (-126 - e1 + r.choice([-1, 0])) * sgn()
        op = r.choice(["fmul.s", "fmadd.s", "fnmsub.s"])
        return (op, bits(A), bits(B), r.choice([0, 0x80000000, 0x00000001, 0x80000001]), 0, rm)
    if k < 0.78:    # exact value just below FLT_MIN that rounds to FLT_MIN: before- and
                    # after-rounding tininess disagree here (RISC-V: after)
        for _ in range(500):
            a = Q(r.randint(2**23, 2**24 - 1), 2**23) * Q(2) ** r.randint(-70, -50)
            b_bits = round_f32(MINN / a * Q(r.randint(2**20 - 64, 2**20 + 64), 2**20), RNE)[0]
            c_bits = r.choice([0, 0x80000001, 0x00000001, 0x80000002])
            q = a * value(b_bits) + value(c_bits)
            if q < MINN and round_f32(q, rm)[0] == 0x00800000:
                return (r.choice(["fmadd.s", "fmul.s"]) if c_bits == 0 else "fmadd.s",
                        bits(a), b_bits, c_bits, 0, rm)
    if k < 0.85:    # fma exact cancellation: a*b - (a*b) = 0, sign depends on rm
        a = Q(r.randint(1, 2**11)) * Q(2) ** r.randint(-20, 20) * sgn()
        b = Q(r.randint(1, 2**11)) * Q(2) ** r.randint(-20, 20) * sgn()
        return (r.choice(["fmsub.s", "fmadd.s"]), bits(a), bits(b), bits(a * b if r.random() < .5 else -a * b), 0, rm)
    # fma near-total cancellation: tiny remainders
    a = Q(r.randint(2**23, 2**24 - 1), 2**23) * sgn()
    b = Q(r.randint(2**23, 2**24 - 1), 2**23)
    c = -a * b
    return ("fmadd.s", bits(a), bits(b), round_f32(c, RNE)[0], 0, rm)

OPS = ["fadd.s", "fsub.s", "fmul.s", "fdiv.s", "fsqrt.s", "fmadd.s", "fmsub.s", "fnmsub.s",
       "fnmadd.s", "fcvt.w.s", "fcvt.wu.s", "fcvt.s.w", "fcvt.s.wu", "feq.s", "flt.s", "fle.s",
       "fmin.s", "fmax.s", "fclass.s", "fsgnj.s", "fsgnjn.s", "fsgnjx.s"]
RMN = ["rne", "rtz", "rdn", "rup", "rmm"]

def expect(op, A, B, C, X, rm):
    if op == "fadd.s":  return f_add(A, B, rm)
    if op == "fsub.s":  return f_add(A, B, rm, sub=True)
    if op == "fmul.s":  return f_mul(A, B, rm)
    if op == "fdiv.s":  return f_div(A, B, rm)
    if op == "fsqrt.s": return f_sqrt(A, rm)
    if op == "fmadd.s":  return f_fma(A, B, C, rm)
    if op == "fmsub.s":  return f_fma(A, B, C, rm, negadd=True)
    if op == "fnmsub.s": return f_fma(A, B, C, rm, negprod=True)
    if op == "fnmadd.s": return f_fma(A, B, C, rm, negprod=True, negadd=True)
    if op == "fcvt.w.s":  return f_cvt_to_int(A, rm, False)
    if op == "fcvt.wu.s": return f_cvt_to_int(A, rm, True)
    if op == "fcvt.s.w":  return f_cvt_from_int(X, rm, False)
    if op == "fcvt.s.wu": return f_cvt_from_int(X, rm, True)
    if op in ("feq.s", "flt.s", "fle.s"): return f_cmp(A, B, op[:3])
    if op == "fmin.s": return f_minmax(A, B, False)
    if op == "fmax.s": return f_minmax(A, B, True)
    if op == "fclass.s": return f_class(A)
    if op == "fsgnj.s":  return (A & 0x7FFFFFFF) | (B & 0x80000000), 0
    if op == "fsgnjn.s": return (A & 0x7FFFFFFF) | (~B & 0x80000000), 0
    if op == "fsgnjx.s": return A ^ (B & 0x80000000), 0

def asm_for(op, rm):
    r = ", " + RMN[rm]
    if op in ("fsqrt.s",):                     return "%s ft3, ft0%s" % (op, r), "f"
    if op.startswith("fcvt.w"):                return "%s t2, ft0%s" % (op, r), "x"
    if op.startswith("fcvt.s"):                return "%s ft3, t1%s" % (op, r), "f"
    if op in ("feq.s", "flt.s", "fle.s"):      return "%s t2, ft0, ft1" % op, "x"
    if op == "fclass.s":                       return "fclass.s t2, ft0", "x"
    if op in ("fmadd.s", "fmsub.s", "fnmsub.s", "fnmadd.s"):
        return "%s ft3, ft0, ft1, ft2%s" % (op, r), "f"
    if op in ("fmin.s", "fmax.s", "fsgnj.s", "fsgnjn.s", "fsgnjx.s"):
        return "%s ft3, ft0, ft1" % op, "f"
    return "%s ft3, ft0, ft1%s" % (op, r), "f"

def run_chunk(lab, cases):
    # program: per case load operands, clear flags, run, store result + flags
    # s0/s1 walk the operand and result arrays: offsets stay within 12 bits
    L = [".text", "main:", "    la s0, ops", "    la s1, out", "    mv s2, s1"]
    for k, (op, A, B, C, X, rm) in enumerate(cases):
        line, kind = asm_for(op, rm)
        L += ["    flw ft0, 0(s0)", "    flw ft1, 4(s0)", "    flw ft2, 8(s0)", "    lw  t1, 12(s0)",
              "    fsflags zero", "    " + line,
              "    fmv.x.w t2, ft3" if kind == "f" else "    nop",
              "    frflags t3", "    sw t2, 0(s1)", "    sw t3, 4(s1)",
              "    addi s0, s0, 16", "    addi s1, s1, 8"]
    L += ["    addi a7, zero, 10", "    ecall", ".data", "ops:"]
    for (op, A, B, C, X, rm) in cases:
        L.append("    .word 0x%08x, 0x%08x, 0x%08x, 0x%08x" % (A, B, C, X))
    L += ["out:", "    .word 0"]
    with tempfile.NamedTemporaryFile("w", suffix=".s", delete=False) as fh:
        fh.write("\n".join(L) + "\n")
        path = fh.name
    binary = os.path.join(lab, "rvsim")
    regs = subprocess.run([binary, path, "--batch", "--single-cycle"], capture_output=True, text=True).stdout
    m = re.search(r"x18/s2  =0x([0-9a-f]+)", regs)
    if not m:
        sys.exit("simulator did not run the generated program (%s):\n%s" % (path, regs[-600:]))
    out_addr = int(m.group(1), 16)
    dump = subprocess.run([binary, path], input="run\nmem %d %d\nquit\n" % (out_addr, 8 * len(cases)),
                          capture_output=True, text=True).stdout
    bytes_ = [int(x, 16) for line in dump.splitlines() if re.match(r"0x[0-9a-f]+: ", line)
              for x in line.split(":", 1)[1].split()]
    words = [int.from_bytes(bytes(bytes_[i:i + 4]), "little") for i in range(0, len(bytes_), 4)]
    os.unlink(path)
    return words

def main():
    lab, n = sys.argv[1], int(sys.argv[2])
    r = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    cases = []
    for _ in range(n):
        if r.random() < 0.4:
            cases.append(targeted(r))
            continue
        op = r.choice(OPS)
        cases.append((op, rand_float(r), rand_float(r), rand_float(r), rand_int(r), r.randrange(5)))
    words = []
    for start in range(0, n, 500):          # the assembler takes at most 8192 lines
        words += run_chunk(lab, cases[start:start + 500])
    bad = 0
    for k, (op, A, B, C, X, rm) in enumerate(cases):
        got_v, got_f = words[2 * k], words[2 * k + 1]
        exp_v, exp_f = expect(op, A, B, C, X, rm)
        exp_v &= 0xFFFFFFFF
        if (got_v, got_f) != (exp_v, exp_f):
            bad += 1
            if bad <= 25:
                print("MISMATCH %-9s %s A=%08x B=%08x C=%08x X=%08x  got %08x/%02x  want %08x/%02x"
                      % (op, RMN[rm], A, B, C, X, got_v, got_f, exp_v, exp_f))
    print("fpref: %d cases, %d mismatches" % (n, bad))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
