#!/usr/bin/env python3
"""Verify the published tables in Experimental tables/ against the thesis results.

Every check here restates a result from the thesis and applies it to the
committed data. The theorem statements are inlined, so this script needs only
the .tsv files -- no PDF, no build, no third-party packages.

    python tests/verify_against_thesis.py                  # table checks only
    python tests/verify_against_thesis.py --solver build/egz-solver

Exits 0 if every check passes, 1 otherwise.
"""

import argparse
import io
import os
import re
import subprocess
import sys
import tempfile
from math import comb

# --- ring metadata -----------------------------------------------------------
# order = number of elements, char = characteristic. `t_limit` is the exclusive
# upper bound on t the table was generated with, as a function of m. Most tables
# used a flat bound (the full header width), but EGZ_Z_2x_by_x2.tsv was produced
# with the variable form that survives commented out in config.hpp:
# smallestPowerBiggerThan(2, m) + m + 1. Cells past a row's limit were never
# computed and so carry "?".
FLAT = None

# Marks a cell holding no value: never computed for that row, or abandoned under
# --max-work. Distinct from blank, which means "no EGZ constant exists".
ABANDONED = "?"

RINGS = {
    "EGZ_Z_3.tsv": dict(order=3, char=3, cyclic=3, t_limit=FLAT),
    "EGZ_Z_4.tsv": dict(order=4, char=4, cyclic=4, t_limit=FLAT),
    "EGZ_Z_5.tsv": dict(order=5, char=5, cyclic=5, t_limit=FLAT),
    "EGZ_Z_6.tsv": dict(order=6, char=6, cyclic=6, t_limit=FLAT),
    "EGZ_Z_7.tsv": dict(order=7, char=7, cyclic=7, t_limit=FLAT),
    "EGZ_Z_8.tsv": dict(order=8, char=8, cyclic=8, t_limit=FLAT),
    # 4-element rings of characteristic 2 -- not Z_4.
    "EGZ_Z_2^2.tsv": dict(order=4, char=2, cyclic=None, t_limit=FLAT),
    "EGZ_F_4.tsv": dict(order=4, char=2, cyclic=None, t_limit=FLAT),
    "EGZ_Z_2x_by_x2.tsv": dict(
        order=4,
        char=2,
        cyclic=None,
        t_limit=lambda m: smallest_power_bigger_than(2, m) + m + 1,
    ),
}


def is_prime(n):
    return n > 1 and all(n % d for d in range(2, int(n**0.5) + 1))


def v_p(m, p):
    """Largest e with p^e | m."""
    e = 0
    while m % p == 0:
        m //= p
        e += 1
    return e


def smallest_power_bigger_than(base, value):
    """Mirrors smallestPowerBiggerThan() in config.hpp: strictly greater."""
    i = 1
    while i <= value:
        i *= base
    return i


def load(path):
    """Returns {m: {t: EGZ(t, m) - t}} for the non-blank cells.

    A row places column t at field index t; see "Output format" in README.md.
    """
    raw = io.open(path, "rb").read().decode("utf-8")
    rows = {}
    # splitlines() so the checks do not depend on the checkout's line endings.
    for line in raw.splitlines()[1:]:
        f = line.split("\t")
        if not f or not f[0]:
            continue
        # "?" marks a cell abandoned under --max-work: no value to check.
        rows[int(f[0])] = {
            t: int(f[t]) for t in range(1, len(f)) if f[t] and f[t] != ABANDONED
        }
    return rows


def widths(path):
    raw = io.open(path, "rb").read().decode("utf-8")
    lines = [l for l in raw.splitlines() if l]
    return lines[0].split("\t"), [len(l.split("\t")) for l in lines]


class Report:
    def __init__(self):
        self.rows = []
        self.failed = False

    def add(self, name, ok, detail):
        self.rows.append((name, ok, detail))
        if not ok:
            self.failed = True

    def show(self):
        width = max(len(n) for n, _, _ in self.rows)
        print()
        for name, ok, detail in self.rows:
            print(f"  {'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
        print()
        print("ALL CHECKS PASSED" if not self.failed else "SOME CHECKS FAILED")


# --- structural --------------------------------------------------------------
def check_shape(tables, rep):
    bad = []
    for name, path in tables:
        hdr, counts = widths(path)
        if hdr[0] != "" or any(hdr[i] != str(i) for i in range(1, len(hdr))):
            bad.append(f"{name}: header labels not 0..n")
        if len(set(counts)) != 1:
            bad.append(f"{name}: ragged rows {sorted(set(counts))}")
    rep.add(
        "layout (header labels, uniform width)",
        not bad,
        "; ".join(bad) or f"{len(tables)} tables",
    )


def check_blank_rule(tables, rep):
    """A blank cell means one thing only: it was computed and no EGZ constant
    exists, which for these rings is exactly when char(R) does not divide
    C(t, m). Every cell outside a row's computed range -- left of the diagonal,
    or past its t_limit -- must carry "?" instead."""
    checked = bad = 0
    detail = []
    for name, path in tables:
        meta = RINGS[name]
        ch, limit = meta["char"], meta["t_limit"]
        raw = io.open(path, "rb").read().decode("utf-8")
        table_bad = 0
        for line in raw.splitlines()[1:]:
            f = line.split("\t")
            if not f or not f[0]:
                continue
            m = int(f[0])
            t_stop = limit(m) if limit else len(f)
            for t in range(1, len(f)):
                checked += 1
                if t < m or t >= t_stop:
                    ok = f[t] == ABANDONED  # never computed, so never blank
                elif f[t] == ABANDONED:
                    continue  # gave up under --max-work; says nothing either way
                else:
                    ok = (f[t] == "") == (comb(t, m) % ch != 0)
                if not ok:
                    bad += 1
                    table_bad += 1
        if table_bad:
            detail.append(f"{name}:{table_bad}")
    rep.add(
        "blank iff computed and char(R) does not divide C(t,m)",
        bad == 0,
        f"{checked} cells, {bad} inconsistent"
        + (" [" + ", ".join(detail) + "]" if detail else ""),
    )


# --- exact results -----------------------------------------------------------
def check_power_formula(tables, rep, name, base, coef, label):
    """EGZ(t, R, base^k) - t = coef * base^k, for every k with a row."""
    path = dict(tables).get(name)
    if path is None:
        return
    rows = load(path)
    ok = bad = 0
    k = 0
    while base**k <= max(rows):
        m = base**k
        for t, v in rows.get(m, {}).items():
            if v == coef * m:
                ok += 1
            else:
                bad += 1
        k += 1
    rep.add(label, bad == 0, f"{ok} cells confirmed, {bad} disagree")


def check_theorem_3_3(tables, rep):
    """Theorem 3.3. Let t and m be positive integers with 2 | C(t,m) and 2^r || m.
    Then, whenever 2|t or m = 2^r:
        EGZ(t, Z_2^2, m) = t + 2*2^r + e,   e = 1 if t = 2m = 2^(r+1), else 0.

    The `2|t` in the domain is a t, not an m: the PDF's math glyphs carry no
    Unicode mapping, and this reading was confirmed against the source. Under
    `2|m` instead, 8 cells at m = 10, 18, 20, 26 disagree.
    """
    path = dict(tables).get("EGZ_Z_2^2.tsv")
    if path is None:
        return
    rows = load(path)
    ok = bad = outside = 0
    for m, cells in rows.items():
        r = v_p(m, 2)
        for t, v in cells.items():
            if not (t % 2 == 0 or m == 2**r):
                outside += 1  # theorem makes no claim here
                continue
            e = 1 if (t == 2 * m and m == 2**r) else 0
            if v == 2 * 2**r + e:
                ok += 1
            else:
                bad += 1
    rep.add(
        "Thm 3.3  Z_2^2, 2|t or m=2^r",
        bad == 0,
        f"{ok} cells confirmed, {bad} disagree ({outside} outside domain)",
    )


def check_theorem_3_5(tables, rep):
    """Theorem 3.5. Let m not be a power of 2, written

        m = 2^r + 2^l + d*2^(l+1) + c*2^(l+2)

    where 2^r and 2^l are the smallest and second smallest non-zero digits of
    the binary representation of m, d is the digit left of 2^l, and c the
    remaining digits. Then

        EGZ(t, Z_4, m) = t + 2^r + e + 2   if l = r + 1
                         t + 3*2^r + e     otherwise

    with e = 1 if t is odd and d = r = 0, else 0.

    Together with Theorem 3.4 (m = 2^k) this covers every row of the Z_4 table.
    """
    path = dict(tables).get("EGZ_Z_4.tsv")
    if path is None:
        return
    rows = load(path)
    ok = bad = 0
    fails = []
    for m, cells in sorted(rows.items()):
        bits = [i for i in range(m.bit_length()) if m >> i & 1]
        if len(bits) < 2:
            continue  # power of two: Theorem 3.4's case, not this one
        r, l = bits[0], bits[1]
        d = (m >> (l + 1)) & 1
        for t, v in sorted(cells.items()):
            e = 1 if (t % 2 == 1 and d == 0 and r == 0) else 0
            pred = (2**r + e + 2) if l == r + 1 else (3 * 2**r + e)
            if v == pred:
                ok += 1
            else:
                bad += 1
                if len(fails) < 6:
                    fails.append((m, t, v, pred, r, l, d))
    detail = f"{ok} cells confirmed, {bad} disagree"
    if fails:
        detail += "  e.g. " + "; ".join(
            f"m={m} t={t} got {v} want {p} (r={r},l={L},d={d})"
            for m, t, v, p, r, L, d in fails[:3]
        )
    rep.add("Thm 3.5  Z_4, m not a power of 2", bad == 0, detail)


def check_propositions_4_1_4_2(tables, rep):
    """Propositions 4.1/4.2 (CONJECTURED): EGZ(t, Z_{p^n}, p^k) = t + (p^n - 1)p^k.

    Agreement is mutual consistency, not independent confirmation.
    """
    cases = [
        ("EGZ_Z_3.tsv", 3, 1),
        ("EGZ_Z_5.tsv", 5, 1),
        ("EGZ_Z_7.tsv", 7, 1),
        ("EGZ_Z_4.tsv", 2, 2),
        ("EGZ_Z_8.tsv", 2, 3),
    ]
    ok = bad = 0
    lookup = dict(tables)
    for name, p, n in cases:
        if name not in lookup:
            continue
        rows = load(lookup[name])
        q = p**n
        k = 0
        while p**k <= max(rows):
            m = p**k
            for t, v in rows.get(m, {}).items():
                if v == (q - 1) * m:
                    ok += 1
                else:
                    bad += 1
            k += 1
    rep.add(
        "Prop 4.1/4.2  m = p^k  (conjecture)",
        bad == 0,
        f"{ok} cells consistent, {bad} disagree",
    )


# --- upper bounds ------------------------------------------------------------
def check_theorem_3_9(tables, rep):
    """Theorem 3.9 (Caro-Schmitt). If n | C(t,m):
        EGZ(t, Z_n, m) <= (n-1)(t-1) + t - m + 1 = n(t-1) - m + 2
    Stated for cyclic Z_n, so only the Z_n tables are checked.
    """
    checked = viol = 0
    for name, path in tables:
        n = RINGS[name]["cyclic"]
        if n is None:
            continue
        for m, cells in load(path).items():
            for t, v in cells.items():
                checked += 1
                if v + t > n * (t - 1) - m + 2:
                    viol += 1
    rep.add("Thm 3.9  bound, Z_n", viol == 0, f"{checked} cells, {viol} violations")


def check_theorem_3_8(tables, rep):
    """Theorem 3.8. For a ring R of prime characteristic p with s elements, and
    p^l the smallest power of p bigger than m:
        EGZ(t, R, m) <= t + s(s-1)(p^l - 1) - m + 2

    p^l is strictly greater than m, matching smallestPowerBiggerThan() in
    config.hpp and the thesis's table captions. Reading it as p^l >= m instead
    collapses the bound to t+1 at m=1 and produces 96 spurious violations.
    """
    checked = viol = 0
    for name, path in tables:
        meta = RINGS[name]
        p, s = meta["char"], meta["order"]
        if not is_prime(p):
            continue  # theorem requires prime characteristic
        for m, cells in load(path).items():
            bound_excess = s * (s - 1) * (smallest_power_bigger_than(p, m) - 1) - m + 2
            for t, v in cells.items():
                checked += 1
                if v > bound_excess:
                    viol += 1
    rep.add(
        "Thm 3.8  bound, prime char", viol == 0, f"{checked} cells, {viol} violations"
    )


# --- solver-based ------------------------------------------------------------
def check_theorem_2_2(solver, rep, m_max=20, t_max=30):
    """Theorem 2.2. If C(t,m) is even and 2^r || m, then EGZ(t, Z_2, m) = t + 2^r.

    Exact for every m, not just prime powers, so this exercises the solver on
    the same code path as the non-prime-power rows of the published tables.
    There is no committed Z_2 table, so one is generated into a temp directory.
    """
    # Windows CreateProcess rejects a relative executable path written with
    # forward slashes, even though os.path.exists accepts it -- normalise first.
    solver = os.path.abspath(solver)
    # Accept the Unix-style path on Windows too, so the invocation in the
    # docstring works on both.
    if not os.path.exists(solver) and os.path.exists(solver + ".exe"):
        solver += ".exe"
    if not os.path.exists(solver):
        rep.add("Thm 2.2  Z_2, all m (solver)", False, f"no such binary: {solver}")
        return
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [
            solver,
            "--ring",
            "Z_2",
            "--m-max",
            str(m_max),
            "--t-max",
            str(t_max),
            "--quiet",
            "--out-dir",
            tmp,
        ]
        try:
            proc = subprocess.run(cmd, capture_output=True, timeout=600)
        except (OSError, subprocess.TimeoutExpired) as exc:
            rep.add(
                "Thm 2.2  Z_2, all m (solver)", False, f"could not run solver: {exc}"
            )
            return
        if proc.returncode != 0:
            rep.add(
                "Thm 2.2  Z_2, all m (solver)",
                False,
                f"solver exited {proc.returncode}: {proc.stderr.decode(errors='replace').strip()[:120]}",
            )
            return
        path = os.path.join(tmp, "EGZ_Z_2.tsv")
        if not os.path.exists(path):
            rep.add("Thm 2.2  Z_2, all m (solver)", False, "solver wrote no table")
            return
        rows = load(path)
    ok = bad = 0
    for m, cells in rows.items():
        pred = 2 ** v_p(m, 2)
        for t, v in cells.items():
            if v == pred:
                ok += 1
            else:
                bad += 1
    rep.add(
        "Thm 2.2  Z_2, all m (solver)",
        bad == 0 and ok > 0,
        f"{ok} cells confirmed, {bad} disagree",
    )


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument(
        "--tables",
        default=os.path.join(os.path.dirname(here), "Experimental tables"),
        help="directory holding the EGZ_*.tsv tables",
    )
    ap.add_argument(
        "--solver", help="path to the egz-solver binary; enables the Theorem 2.2 check"
    )
    args = ap.parse_args()

    tables = []
    for name in sorted(RINGS):
        path = os.path.join(args.tables, name)
        if os.path.exists(path):
            tables.append((name, path))
    if not tables:
        print(f"no tables found in {args.tables}", file=sys.stderr)
        return 2
    print(
        f"Verifying {len(tables)} tables in {args.tables} against the thesis results."
    )

    rep = Report()
    check_shape(tables, rep)
    check_blank_rule(tables, rep)
    check_power_formula(tables, rep, "EGZ_Z_3.tsv", 3, 2, "Thm 3.1  Z_3, m = 3^k")
    check_power_formula(tables, rep, "EGZ_Z_4.tsv", 2, 3, "Thm 3.4  Z_4, m = 2^k")
    check_theorem_3_3(tables, rep)
    check_theorem_3_5(tables, rep)
    check_propositions_4_1_4_2(tables, rep)
    check_theorem_3_9(tables, rep)
    check_theorem_3_8(tables, rep)
    if args.solver:
        check_theorem_2_2(args.solver, rep)
    rep.show()
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
