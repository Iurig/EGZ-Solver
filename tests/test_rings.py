#!/usr/bin/env python3
"""Check the rings themselves: axioms, stability, and the quotient equivalences.

    python tests/test_rings.py path/to/dump_ring [--solver path/to/egz-solver]

Five things are checked, in increasing order of what they buy you.

1. Every registered ring satisfies the commutative-unit-ring axioms. The solver
   assumes them everywhere and would not fail loudly if they broke: a bad
   multiplication table yields wrong EGZ constants, not a crash.

2. Every registered ring still matches its golden dump. The published tables in
   Experimental tables/ were computed with these exact operation tables, so a
   silent change to one invalidates data already committed.

3. A reference implementation of Z_n[x]/(P) in this file, independent of
   anything in C++, reproduces the goldens of the rings it generalises. It needs
   no build, and it is what pins down the encoding: basis {1, x, ..., x^(d-1)}
   with an element (a_0..a_(d-1)) at index sum(a_i * n^i).

4. The runtime quotient in quotient.hpp, asked for by --ring spec, agrees with
   both the reference and the golden -- and reports a name that can go in a file
   name, since it becomes EGZ_<name>.tsv. A spec the binary does not accept
   reports PEND rather than passing silently.

5. With --solver, a small EGZ table computed for the spec is compared against
   one computed for the ring it generalises. Equal operation tables do not by
   themselves mean equal output: this is the check that the memo, e_m and the
   counterexample search behave the same too.

Point 3 was written before the implementation existed, which is why the target
is a fixed thing to hit rather than one fitted to whatever came out.
"""

import argparse
import io
import os
import re
import subprocess
import sys
import tempfile

# --- what the planned generic quotient ring has to reproduce ------------------
# Under the encoding above, Z_n[x]/(P) does not merely become *isomorphic* to
# the ring it generalises, it becomes *identical*: same elements, same indices,
# same tables. That is what makes an exact table comparison the right check.
#
# Polynomials are coefficient lists, lowest degree first, and must be monic --
# the leading coefficient has to be a unit for reduction to be well defined, and
# 1 is the only one guaranteed to be one in Z_n for composite n.
#
# Candidate names follow the existing convention, Z_<n>x_by_<poly>, which
# already renders Z_2[x]/(x^2) as "Z_2x_by_x2" -- the name the committed table
# EGZ_Z_2x_by_x2.tsv is keyed on. If the naming scheme changes these change with
# it; nothing else here depends on the spelling.
EXPECTED = [
    # (--ring spec, n, P, golden it must equal, what it demonstrates)
    ("Z_2[x]/(x)", 2, [0, 1], "Z_2", "Z_2[x]/(x) recovers Z_2"),
    ("Z_3[x]/(x)", 3, [0, 1], "Z_3", "Z_3[x]/(x) recovers Z_3"),
    ("Z_5[x]/(x)", 5, [0, 1], "Z_5", "Z_5[x]/(x) recovers Z_5"),
    ("Z_2[x]/(x^2)", 2, [0, 0, 1], "Z_2x_by_x2", "Z_2[x]/(x^2) recovers the hand-written Z_2_over"),
    ("Z_2[x]/(x^2+x+1)", 2, [1, 1, 1], "F_4", "Z_2[x]/(x^2+x+1) recovers the hand-written F_4"),
]

# A ring's name becomes part of a file name, EGZ_<name>.tsv, so a runtime ring
# cannot simply be called by its spec: "Z_2[x]/(x^2)" has a path separator in
# it. Whatever the parser produces has to survive this.
SAFE_NAME = re.compile(r"^[A-Za-z0-9_^+.-]+$")

failures = []
pending = []


def check(name, cond, detail=""):
    print("  %s  %s%s" % ("PASS" if cond else "FAIL", name, "  " + detail if detail else ""))
    if not cond:
        failures.append(name)


class Ring:
    """A ring's structure, as dumped by dump_ring or built by reference()."""

    def __init__(self, name, order, characteristic, unit, add, mul):
        self.name = name
        self.order = order
        self.characteristic = characteristic
        self.unit = unit
        self.add = add
        self.mul = mul

    def key(self):
        return (self.order, self.characteristic, self.unit, self.add, self.mul)


# --- the reference quotient ---------------------------------------------------
def reference(n, poly, name="reference"):
    """Tables for Z_n[x]/(P), P = poly[0] + poly[1] x + ... + x^d, monic.

    Deliberately the most direct thing that could work -- multiply out, then
    fold every x^j with j >= d down using x^d = -(poly[0] + ... + poly[d-1]
    x^(d-1)). No cleverness, because its job is to be obviously correct and to
    disagree with a subtly wrong implementation.
    """
    d = len(poly) - 1
    assert d >= 1, "P must have degree at least 1; degree 0 gives the zero ring"
    assert poly[-1] % n == 1, "P must be monic"
    order = n ** d

    def to_vec(k):
        v = []
        for _ in range(d):
            v.append(k % n)
            k //= n
        return v

    def to_idx(v):
        k = 0
        for i in reversed(range(d)):
            k = k * n + v[i] % n
        return k

    def fold(v):
        v = list(v)
        for j in range(len(v) - 1, d - 1, -1):
            c = v[j] % n
            if c:
                v[j] = 0
                for i in range(d):
                    v[j - d + i] -= c * poly[i]
        return [x % n for x in v[:d]]

    add = [[0] * order for _ in range(order)]
    mul = [[0] * order for _ in range(order)]
    for i in range(order):
        a = to_vec(i)
        for j in range(order):
            b = to_vec(j)
            add[i][j] = to_idx([(x + y) % n for x, y in zip(a, b)])
            prod = [0] * (2 * d - 1)
            for p in range(d):
                for q in range(d):
                    prod[p + q] += a[p] * b[q]
            mul[i][j] = to_idx(fold(prod))
    # The ideal generated by a monic P of degree >= 1 contains no nonzero
    # constant -- any nonzero multiple of P has degree >= d -- so k*1 = 0 in the
    # quotient exactly when n divides k, and the characteristic stays n.
    return Ring(name, order, n, 1, add, mul)


def render(n, poly):
    """P as it appears in a ring name: x2+x+1, x2, x, 2x2+x+1."""
    terms = []
    for i in range(len(poly) - 1, -1, -1):
        c = poly[i] % n
        if not c:
            continue
        coef = "" if (c == 1 and i > 0) else str(c)
        var = "" if i == 0 else ("x" if i == 1 else "x" + str(i))
        terms.append(coef + var)
    return "+".join(terms) if terms else "0"


# --- dump parsing -------------------------------------------------------------
def parse(text):
    """Splits a dump of one or more rings into {name: Ring}."""
    rings, cur = {}, None
    for line in text.splitlines():
        if not line.strip():
            continue
        f = line.split("\t")
        key = f[0]
        if key == "name":
            cur = dict(name=f[1], add={}, mul={})
            rings[f[1]] = cur
        elif key in ("order", "characteristic", "unit"):
            cur[key] = int(f[1])
        elif key in ("add", "mul"):
            cur[key][int(f[1])] = [int(v) for v in f[2:]]
        else:
            raise ValueError("unexpected key %r" % key)
    out = {}
    for name, d in rings.items():
        k = d["order"]
        out[name] = Ring(name, k, d["characteristic"], d["unit"],
                         [d["add"][i] for i in range(k)], [d["mul"][i] for i in range(k)])
    return out


def run(dumper, *a):
    proc = subprocess.run([dumper] + list(a), capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError("%s %s exited %d: %s" % (dumper, a, proc.returncode,
                                                    proc.stderr.decode(errors="replace")[:200]))
    return proc.stdout.decode()


def solver_table(solver, ring, tmp, tag):
    """One small EGZ table for `ring`, as text. None if the solver refused.

    Written into an empty directory of its own because the file is named after
    the ring, and a spec's name is exactly what is under test."""
    out = os.path.join(tmp, tag)
    os.makedirs(out, exist_ok=True)
    proc = subprocess.run([solver, "--ring", ring, "--m-max", "7", "--t-max", "12",
                           "--quiet", "--out-dir", out], capture_output=True)
    if proc.returncode != 0:
        return None
    files = [f for f in os.listdir(out) if f.endswith(".tsv")]
    if len(files) != 1:
        return None
    return io.open(os.path.join(out, files[0]), "rb").read().decode("utf-8")


def try_run(dumper, spec):
    """Asks the binary for one ring by spec. Returns None if it does not know
    it, which is how an unimplemented entry reports PEND rather than failing."""
    proc = subprocess.run([dumper, spec], capture_output=True)
    if proc.returncode != 0:
        return None
    rings = parse(proc.stdout.decode())
    return list(rings.values())[0] if len(rings) == 1 else None


# --- axioms -------------------------------------------------------------------
def axiom_failures(r):
    """The commutative-unit-ring axioms this table set violates, as readable
    strings. Checked over all order^3 triples, which is trivial at the orders
    the solver can search anyway."""
    bad = []
    n, add, mul, e = r.order, r.add, r.mul, r.unit

    if len(add) != n or len(mul) != n or any(len(row) != n for row in add + mul):
        return ["a table is not order x order"]
    if not all(0 <= v < n for row in add + mul for v in row):
        return ["a table entry is outside [0, order)"]
    if not 0 <= e < n:
        return ["unit is outside [0, order)"]

    if any(add[0][a] != a for a in range(n)):
        bad.append("0 is not an additive identity")
    if any(mul[e][a] != a for a in range(n)):
        bad.append("unit is not a multiplicative identity")
    if any(mul[0][a] != 0 for a in range(n)):
        bad.append("0 does not annihilate")
    if any(not any(add[a][b] == 0 for b in range(n)) for a in range(n)):
        bad.append("some element has no additive inverse")
    if any(add[a][b] != add[b][a] for a in range(n) for b in range(n)):
        bad.append("+ is not commutative")
    if any(mul[a][b] != mul[b][a] for a in range(n) for b in range(n)):
        bad.append("* is not commutative")

    for a in range(n):
        for b in range(n):
            for c in range(n):
                if add[add[a][b]][c] != add[a][add[b][c]]:
                    bad.append("+ is not associative")
                if mul[mul[a][b]][c] != mul[a][mul[b][c]]:
                    bad.append("* is not associative")
                if mul[a][add[b][c]] != add[mul[a][b]][mul[a][c]]:
                    bad.append("* does not distribute over +")
            if len(bad) > 6:
                break

    acc, found = 0, None
    for k in range(1, n + 1):
        acc = add[acc][e]
        if acc == 0:
            found = k
            break
    if found != r.characteristic:
        bad.append("characteristic is %s, not the declared %d" % (found, r.characteristic))
    return sorted(set(bad))


def isomorphism_hint(a, b):
    """If two rings differ but are isomorphic, say so: the likely cause is a
    different basis ordering, not the wrong ring. Brute force, so small orders
    only -- the rings these checks target have order <= 8."""
    from itertools import permutations
    if a.order != b.order or a.order > 8:
        return ""
    for p in permutations(range(a.order)):
        if p[0] != 0 or p[a.unit] != b.unit:
            continue
        if all(p[a.add[i][j]] == b.add[p[i]][p[j]] and p[a.mul[i][j]] == b.mul[p[i]][p[j]]
               for i in range(a.order) for j in range(a.order)):
            return " -- isomorphic under relabelling %s, so the ring is right but the basis order is not" % (p,)
    return " -- and not isomorphic either, so this is the wrong ring"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dumper", help="path to the dump_ring binary")
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--goldens", default=os.path.join(here, "ring_goldens.tsv"))
    ap.add_argument("--solver", help="path to egz-solver; adds the end-to-end table comparison")
    args = ap.parse_args()

    # Windows CreateProcess rejects a relative path written with forward slashes.
    dumper = os.path.abspath(args.dumper)
    if not os.path.exists(dumper) and os.path.exists(dumper + ".exe"):
        dumper += ".exe"
    if not os.path.exists(dumper):
        print("no such binary: %s" % dumper, file=sys.stderr)
        return 2

    registered = parse(run(dumper, "--all"))
    goldens = parse(io.open(args.goldens, "rb").read().decode("utf-8"))
    print("%d rings registered, %d goldens on file.\n" % (len(registered), len(goldens)))

    print("Ring axioms:")
    for name in sorted(registered):
        r = registered[name]
        bad = axiom_failures(r)
        check("%-14s commutative unit ring" % name, not bad,
              "; ".join(bad) if bad else "order %d, char %d" % (r.order, r.characteristic))

    print("\nStability against tests/ring_goldens.tsv:")
    missing = sorted(n for n in goldens if n not in registered)
    check("every golden ring is still registered", not missing, ", ".join(missing))
    ungolden = sorted(n for n in registered if n not in goldens)
    check("every registered ring has a golden", not ungolden,
          "regenerate with: dump_ring --all > tests/ring_goldens.tsv  (missing: %s)" % ", ".join(ungolden)
          if ungolden else "")
    for name in sorted(set(registered) & set(goldens)):
        same = registered[name].key() == goldens[name].key()
        check("%-14s unchanged" % name, same,
              "" if same else "tables differ from the golden" + isomorphism_hint(registered[name], goldens[name]))

    print("\nReference Z_n[x]/(P) against the goldens (runs without the C++):")
    for spec, n, poly, target, why in EXPECTED:
        if target not in goldens:
            check("%-18s vs %s" % (spec, target), False, "no golden named %s" % target)
            continue
        ref = reference(n, poly, spec)
        bad = axiom_failures(ref)
        if bad:
            check("%-18s is a ring" % spec, False, "; ".join(bad))
            continue
        same = ref.key() == goldens[target].key()
        check("%-18s %s" % (spec, why), same,
              "" if same else "differs from the %s golden%s" % (target, isomorphism_hint(ref, goldens[target])))

    print("\nThe implementation, asked for each ring by --ring spec:")
    for spec, n, poly, target, why in EXPECTED:
        got = try_run(dumper, spec)
        if got is None:
            pending.append(spec)
            print("  PEND  %-18s %s -- dump_ring does not accept this spec yet" % (spec, why))
            continue
        ref = reference(n, poly, spec)
        # The name becomes EGZ_<name>.tsv, so it cannot be the spec verbatim.
        ok_name = bool(SAFE_NAME.match(got.name))
        check("%-18s has a file-safe name" % spec, ok_name,
              repr(got.name) if ok_name else "%r cannot go in EGZ_<name>.tsv" % got.name)
        ok_ref = got.key() == ref.key()
        check("%-18s matches the reference Z_%d[x]/(%s)" % (spec, n, render(n, poly)), ok_ref,
              "" if ok_ref else "differs from the reference" + isomorphism_hint(got, ref))
        gold = goldens.get(target)
        if gold is not None:
            ok_gold = got.key() == gold.key()
            check("%-18s %s" % (spec, why), ok_gold,
                  "" if ok_gold else "differs from the %s golden%s" % (target, isomorphism_hint(got, gold)))

    if args.solver:
        solver = os.path.abspath(args.solver)
        if not os.path.exists(solver) and os.path.exists(solver + ".exe"):
            solver += ".exe"
        print("\nEnd to end: a small EGZ table from each pair, through the whole solver:")
        with tempfile.TemporaryDirectory() as tmp:
            for i, (spec, _, _, target, why) in enumerate(EXPECTED):
                a = solver_table(solver, spec, tmp, "a%d" % i)
                b = solver_table(solver, target, tmp, "b%d" % i)
                if a is None or b is None:
                    check("%-18s vs %s" % (spec, target), False,
                          "the solver would not compute %s" % (spec if a is None else target))
                    continue
                # Equal tables here mean more than equal operation tables: the
                # same values came out of e_m, the counterexample search and the
                # memo, not just out of + and *.
                check("%-18s table identical to %s" % (spec, target), a == b,
                      "" if a == b else "tables differ")

    print()
    if pending:
        print("%d planned equivalence(s) awaiting an implementation: %s" % (len(pending), ", ".join(pending)))
        print("The reference checks above already hold, so the target is fixed; only the C++ is missing.")
    if failures:
        print("%d check(s) failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
