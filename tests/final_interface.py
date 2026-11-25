#!/usr/bin/env python3
"""Check final evaluator output, all CLI modes, tracing and cycle limits."""
import pathlib
import re
import subprocess
import tempfile

EXE = pathlib.Path(__file__).resolve().parents[1] / 'lab5' / 'rvsim'


def run(program, *options):
    result = subprocess.run([str(EXE), str(program), *options],
                            capture_output=True, text=True, timeout=10, check=True)
    return result.stdout


with tempfile.TemporaryDirectory() as directory:
    program = pathlib.Path(directory) / 'exit.s'
    # Spacing makes this program valid even with no hazard handling.
    program.write_text('.text\nmain:\naddi a7, zero, 10\nnop\nnop\nnop\necall\n')
    for hazard in ('none', 'stall', 'forward'):
        for predictor in ('not-taken', 'btfn', '1bit', '2bit'):
            out = run(program, '--batch', '--hazard', hazard, '--predictor', predictor)
            assert re.search(r'^pc = 0x00000010$', out, re.M), out
            assert 'status = ECALL_EXIT   cycles = 9' in out, out
            for name, value in dict(retired=5, stalls=0, branches=0, mispredicts=0,
                                    jal=0, jalr=0, redirects=0, squashed=0).items():
                assert re.search(r'^' + name + r'\s*=\s*' + str(value) + r'\s*$', out, re.M), out
    out = run(program, '--batch', '--single-cycle')
    assert 'status = ECALL_EXIT   cycles = 5' in out, out
    assert re.search(r'^pc = 0x00000010$', out, re.M), out
    out = run(program, '--batch', '--trace', '--max-cycles', '2')
    assert 'status = RUNNING   cycles = 2' in out, out
    assert 'IF ' in out and 'WB ' in out, out
    assert 'reached max cycle limit (2)' in out, out

    # ECALL reads a0/a7 in EX: interlock both producers, or forward them.
    program.write_text('.text\nmain:\naddi a0, zero, 7\naddi a7, zero, 1\necall\naddi a7, zero, 10\necall\n')
    for hazard, cycles, stalls in [('forward', 9, 0), ('stall', 13, 4)]:
        out = run(program, '--batch', '--hazard', hazard)
        assert out.splitlines()[1] == '7', out
        assert f'status = ECALL_EXIT   cycles = {cycles}' in out, out
        assert re.search(r'^stalls\s*=\s*' + str(stalls) + r'\s*$', out, re.M), out

    # The WB cycle that delivers an illegal-instruction fault is a clock too.
    program.write_text('.text\nmain:\n.word 0\n')
    out = run(program, '--batch')
    assert 'status = ILLEGAL_INSTRUCTION   cycles = 5' in out, out
    assert re.search(r'^pc = 0x00000000$', out, re.M), out

print('final interface: 12 pipeline modes, single-cycle, trace, cycle limit, ECALL hazards and fault timing passed')
