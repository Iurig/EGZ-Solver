#!/usr/bin/env python3
"""Verify the published tables in Experimental tables/ against the literature.

Most checks restate a result from the thesis; the rest apply classical zero-sum
theorems from the wider literature, which reach only the degree-1 rows -- higher
degrees are what the thesis itself contributes. Every statement is inlined, so
this needs only the .tsv files.

    python tests/verify_against_literature.py              # table checks only
    python tests/verify_against_literature.py --solver build/egz-solver

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
# upper bound on t a table was generated with, as a function of m: cells past a
# row's limit were never computed and must carry "?", not a blank.
#
# Every table now uses the flat bound, the full header width. The variable form
# stays because it has been needed here before: EGZ_Z_2x_by_x2.tsv used
# smallestPowerBiggerThan(2, m) + m + 1 until it was recomputed full width.
FLAT = None

# A cell holding no value: never computed, or abandoned under --max-work.
# Distinct from blank, which means "no EGZ constant exists".
ABANDONED = "?"

# `additive` names the ring's additive group, which is all degree 1 sees; rings
# sharing it are checked against the same group-theoretic results and against
# each other. `dav` is its Davenport constant: n for Z_n, and 1 + sum(p^e - 1)
# for p-groups (Olson 1969).
RINGS = {
    "EGZ_Z_3.tsv": dict(
        order=3, char=3, cyclic=3, t_limit=FLAT, additive=("Z_n", 3), dav=3
    ),
    "EGZ_Z_4.tsv": dict(
        order=4, char=4, cyclic=4, t_limit=FLAT, additive=("Z_n", 4), dav=4
    ),
    "EGZ_Z_5.tsv": dict(
        order=5, char=5, cyclic=5, t_limit=FLAT, additive=("Z_n", 5), dav=5
    ),
    "EGZ_Z_6.tsv": dict(
        order=6, char=6, cyclic=6, t_limit=FLAT, additive=("Z_n", 6), dav=6
    ),
    "EGZ_Z_7.tsv": dict(
        order=7, char=7, cyclic=7, t_limit=FLAT, additive=("Z_n", 7), dav=7
    ),
    "EGZ_Z_8.tsv": dict(
        order=8, char=8, cyclic=8, t_limit=FLAT, additive=("Z_n", 8), dav=8
    ),
    "EGZ_Z_9.tsv": dict(
        order=9, char=9, cyclic=9, t_limit=FLAT, additive=("Z_n", 9), dav=9
    ),
    "EGZ_Z_10.tsv": dict(
        order=10, char=10, cyclic=10, t_limit=FLAT, additive=("Z_n", 10), dav=10
    ),
    "EGZ_Z_11.tsv": dict(
        order=11, char=11, cyclic=11, t_limit=FLAT, additive=("Z_n", 11), dav=11
    ),
    "EGZ_Z_12.tsv": dict(
        order=12, char=12, cyclic=12, t_limit=FLAT, additive=("Z_n", 12), dav=12
    ),
    # Elementary abelian of rank 3: order 8, characteristic 2 -- not Z_8.
    "EGZ_Z_2^3.tsv": dict(
        order=8, char=2, cyclic=None, t_limit=FLAT, additive=("Z_2^d", 3), dav=4
    ),
    # Elementary abelian of rank 2: order 9, characteristic 3 -- not Z_9.
    "EGZ_Z_3^2.tsv": dict(order=9, char=3, cyclic=None, t_limit=FLAT, additive=("Z_3^d", 2), dav=5),
    # F_8 = Z_2[x]/(x^3+x+1): order 8, characteristic 2, additively Z_2^3.
    "EGZ_Z_2x_by_x3+x+1.tsv": dict(
        order=8, char=2, cyclic=None, t_limit=FLAT, additive=("Z_2^d", 3), dav=4
    ),
    # F_9 = Z_3[x]/(x^2+1): order 9, characteristic 3, additively Z_3^2.
    "EGZ_Z_3x_by_x2+1.tsv": dict(
        order=9, char=3, cyclic=None, t_limit=FLAT, additive=("Z_3^d", 2), dav=5
    ),
    # 4-element rings of characteristic 2 -- not Z_4.
    "EGZ_Z_2^2.tsv": dict(
        order=4, char=2, cyclic=None, t_limit=FLAT, additive=("Z_2^d", 2), dav=3
    ),
    "EGZ_F_4.tsv": dict(
        order=4, char=2, cyclic=None, t_limit=FLAT, additive=("Z_2^d", 2), dav=3
    ),
    "EGZ_Z_2x_by_x2.tsv": dict(
        order=4, char=2, cyclic=None, t_limit=FLAT, additive=("Z_2^d", 2), dav=3
    ),
}


def exponent(meta):
    """Exponent of the additive group: n for Z_n, p for an elementary p-group."""
    kind, n = meta["additive"]
    return n if kind == "Z_n" else int(kind[2])


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
    """A blank means one thing only: computed, and no EGZ constant exists --
    for these rings, exactly when char(R) does not divide C(t, m). Everything
    outside a row's computed range, left of the diagonal or past its t_limit,
    must carry "?" instead."""
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


# --- literature, higher degree ----------------------------------------------
def check_caro_schmitt_mod3(tables, rep):
    """Caro-Schmitt (Integers 22, 2022), the mod 3 theorem: for k = 3^a, a >= 2,
    EGZ(k, Z_3, 3) <= k + 6. The only outside bound reaching m >= 2 beyond
    Thm 3.9 and Thm 2.2, which are also theirs. Sharp: the table holds 6."""
    path = dict(tables).get("EGZ_Z_3.tsv")
    if path is None:
        return
    row = load(path).get(3, {})
    checked = viol = 0
    k = 9
    while k in row:
        checked += 1
        if row[k] > 6:
            viol += 1
        k *= 3
    rep.add(
        "Caro-Schmitt  Z_3 degree 3, t = 3^a",
        viol == 0,
        f"{checked} cells, {viol} violations",
    )


# --- literature, degree 1 --------------------------------------------------
# EGZ(t, R, 1) - t is the classical zero-sum constant s_t(G) - t of the additive
# group G of R: e_1 is the plain sum. These checks apply results from the wider
# zero-sum literature to that row; higher degrees have no literature to compare
# against, being what the thesis contributes.
def check_egz_constants(tables, rep):
    """s(G), the EGZ constant proper, sits at (m=1, t=exp(G)) as s(G) - exp(G).
    Known exactly for every additive group here:
      s(Z_n)   = 2n - 1      Erdos-Ginzburg-Ziv 1961
      s(Z_2^d) = 2^d + 1     Harborth 1973
      s(Z_p^2) = 4p - 3      Kemnitz's conjecture, proved by Reiher 2007
    """
    ok = bad = 0
    fails = []
    for name, path in tables:
        meta = RINGS[name]
        kind, n = meta["additive"]
        if kind == "Z_n":
            s_g = 2 * n - 1
        elif kind.endswith("^d") and int(kind[2]) == 2:
            s_g = 2**n + 1
        elif kind.endswith("^d") and n == 2:
            s_g = 4 * int(kind[2]) - 3
        else:
            continue
        e = exponent(meta)
        got = load(path).get(1, {}).get(e)
        if got == s_g - e:
            ok += 1
        else:
            bad += 1
            fails.append(f"{name}: got {got} want {s_g - e}")
    rep.add(
        "EGZ/Harborth/Reiher  s(G) at (1, exp)",
        bad == 0,
        f"{ok} tables confirmed, {bad} disagree"
        + ("  " + "; ".join(fails[:3]) if fails else ""),
    )


def check_gao_davenport(tables, rep):
    """Gao 1996: for exp(G) | t and t >= |G|,  s_t(G) = t + D(G) - 1. With
    Olson's Davenport constants this pins the whole tail of every m = 1 row.
    Wrongly blank cells in the domain are the blank-rule check's to catch."""
    ok = bad = 0
    fails = []
    for name, path in tables:
        meta = RINGS[name]
        e, want = exponent(meta), meta["dav"] - 1
        for t, v in load(path).get(1, {}).items():
            if t % e or t < meta["order"]:
                continue
            if v == want:
                ok += 1
            else:
                bad += 1
                if len(fails) < 3:
                    fails.append(f"{name} t={t}: got {v} want {want}")
    rep.add(
        "Gao+Olson  m=1 tail is D(G) - 1",
        bad == 0,
        f"{ok} cells confirmed, {bad} disagree"
        + ("  " + "; ".join(fails) if fails else ""),
    )


def check_davenport_lower(tables, rep):
    """The one classical lower bound: s_t(G) >= t + D(G) - 1 for every t, from
    the standard construction -- a zero-sum-free sequence of length D(G) - 1
    padded with t - 1 zeros has no length-t zero-sum. Gao pins the t >= |G|
    cells exactly; this also reaches the small-t cells before that, where the
    values sit strictly above the bound."""
    checked = viol = 0
    fails = []
    for name, path in tables:
        floor = RINGS[name]["dav"] - 1
        for t, v in load(path).get(1, {}).items():
            checked += 1
            if v < floor:
                viol += 1
                if len(fails) < 3:
                    fails.append(f"{name} t={t}: {v} < {floor}")
    rep.add(
        "Davenport lower bound on m=1",
        viol == 0,
        f"{checked} cells, {viol} violations" + ("  " + "; ".join(fails) if fails else ""),
    )


def check_additive_agreement(tables, rep):
    """Degree 1 sees only (R, +), so rings with the same additive group must
    produce identical m = 1 rows -- blanks included -- wherever both computed
    them. Cross-checks F_4, Z_2^2 and Z_2[x]/(x^2) against each other."""

    def raw_row_1(path):
        raw = io.open(path, "rb").read().decode("utf-8")
        for line in raw.splitlines()[1:]:
            f = line.split("	")
            if f and f[0] == "1":
                return f
        return None

    groups = {}
    for name, path in tables:
        groups.setdefault(RINGS[name]["additive"], []).append((name, path))
    ok = bad = 0
    fails = []
    for group in groups.values():
        if len(group) < 2:
            continue
        base_name, base_path = group[0]
        base = raw_row_1(base_path)
        for name, path in group[1:]:
            row = raw_row_1(path)
            for t in range(1, min(len(base), len(row))):
                if base[t] == ABANDONED or row[t] == ABANDONED:
                    continue
                if base[t] == row[t]:
                    ok += 1
                else:
                    bad += 1
                    if len(fails) < 3:
                        fails.append(
                            f"{base_name} vs {name} t={t}: {base[t]!r} vs {row[t]!r}"
                        )
    rep.add(
        "additive groups agree on m=1",
        bad == 0,
        f"{ok} cells compared, {bad} disagree"
        + ("  " + "; ".join(fails) if fails else ""),
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
        f"Verifying {len(tables)} tables in {args.tables} against the thesis and the literature."
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
    check_caro_schmitt_mod3(tables, rep)
    check_egz_constants(tables, rep)
    check_gao_davenport(tables, rep)
    check_davenport_lower(tables, rep)
    check_additive_agreement(tables, rep)
    if args.solver:
        check_theorem_2_2(args.solver, rep)
    rep.show()
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
