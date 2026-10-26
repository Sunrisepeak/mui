#!/usr/bin/env python3
"""Correctly rounded reference values for openmui-anim's elementary functions.

    python3 gen_math_reference.py > math-reference.tsv     (needs mpmath)

Each line: function, argument(s) and the correctly rounded result, as
hexadecimal floating point (exact). The arguments come from a fixed generator,
so regenerating gives the same file; the test reads it and measures ulps
against these values instead of against the platform's own libm, whose last
bits differ between glibc, Apple and Windows.
"""
import random
import mpmath

mpmath.mp.prec = 256
rng = random.Random(20261026)

def row(name, args, value):
    print("\t".join([name] + [float(a).hex() for a in args] + [float(value).hex()]))

def sample(lo, hi, n):
    return [rng.uniform(lo, hi) for _ in range(n)]

for x in sample(-700, 700, 600) + sample(-2, 2, 400):
    row("exp", [x], mpmath.exp(mpmath.mpf(x)))
for x in [10 ** rng.uniform(-300, 300) for _ in range(600)] + sample(0.5, 2, 400):
    row("log", [x], mpmath.log(mpmath.mpf(x)))
for x in sample(-100, 100, 600) + sample(-0.78, 0.78, 200) + sample(-1e5, 1e5, 200):
    row("sin", [x], mpmath.sin(mpmath.mpf(x)))
    row("cos", [x], mpmath.cos(mpmath.mpf(x)))
for x in sample(-50, 50, 600) + sample(-1, 1, 400):
    row("atan", [x], mpmath.atan(mpmath.mpf(x)))
for _ in range(1000):
    y, x = rng.uniform(-10, 10), rng.uniform(-10, 10)
    row("atan2", [y, x], mpmath.atan2(mpmath.mpf(y), mpmath.mpf(x)))
for x in sample(-1e6, 1e6, 600) + sample(-1, 1, 400):
    row("cbrt", [x], mpmath.cbrt(mpmath.mpf(x)) if x >= 0 else -mpmath.cbrt(mpmath.mpf(-x)))
for _ in range(1000):
    x, y = rng.uniform(0.001, 1.0), rng.uniform(0.2, 3.0)
    row("pow", [x, y], mpmath.power(mpmath.mpf(x), mpmath.mpf(y)))
