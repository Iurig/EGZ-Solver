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
    # (--ring spec, n, relations, golden it must equal, what it demonstrates)
    ("Z_2[x]/(x)", 2, [[0, 1]], "Z_2", "Z_2[x]/(x) recovers Z_2"),
    ("Z_3[x]/(x)", 3, [[0, 1]], "Z_3", "Z_3[x]/(x) recovers Z_3"),
    ("Z_5[x]/(x)", 5, [[0, 1]], "Z_5", "Z_5[x]/(x) recovers Z_5"),
    ("Z_2[x]/(x^2)", 2, [[0, 0, 1]], "Z_2x_by_x2", "Z_2[x]/(x^2) recovers the hand-written Z_2_over"),
    ("Z_2[x]/(x^2+x+1)", 2, [[1, 1, 1]], "F_4", "Z_2[x]/(x^2+x+1) recovers the hand-written F_4"),
    # Several variables. A relation of degree 1 kills its variable, so these
    # collapse onto rings already on file -- which is what makes them checkable
    # at all: there is no committed table for a genuinely new ring to match.
    ("Z_2[x,y]/(x,y)", 2, [[0, 1], [0, 1]], "Z_2", "two dead variables still recover Z_2"),
    ("Z_3[x,y]/(x+1,y)", 3, [[1, 1], [0, 1]], "Z_3", "a shifted dead variable still recovers Z_3"),
    ("Z_2[x,y]/(x^2,y)", 2, [[0, 0, 1], [0, 1]], "Z_2x_by_x2", "Z_2[x,y]/(x^2,y) recovers Z_2_over"),
    ("Z_2[x,y]/(x^2+x+1,y)", 2, [[1, 1, 1], [0, 1]], "F_4", "Z_2[x,y]/(x^2+x+1,y) recovers F_4"),
]

# Multivariate rings with no golden to match: nothing in the repository is
# isomorphic to them, so all that can be asked is that they are rings and that
# the C++ and the reference agree on which one.
#
# Both checks are cubic or worse in the order, in Python, so these stay small on
# purpose -- the largest here already takes longer than the rest of the file put
# together. Unequal degrees per variable earn their place: they would catch a
# basis that assumed every variable contributes the same number of monomials,
# which three squares would not.
NEW_RINGS = [
    ("Z_2[x,y]/(x^2,y^2)", 2, [[0, 0, 1], [0, 0, 1]], 16),
    ("Z_2[x,y]/(x^2+x+1,y^2)", 2, [[1, 1, 1], [0, 0, 1]], 16),
    ("Z_3[x,y]/(x,y^2)", 3, [[0, 1], [0, 0, 1]], 9),
    ("Z_2[x,y]/(x^2,y^3)", 2, [[0, 0, 1], [0, 0, 0, 1]], 64),
    ("Z_2[x,y,z]/(x,y,z^2)", 2, [[0, 1], [0, 1], [0, 0, 1]], 4),
    ("Z_2[x,y,z]/(x^2,y,z^3)", 2, [[0, 0, 1], [0, 1], [0, 0, 0, 1]], 64),
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
def reference(n, polys, name="reference"):
    """Tables for Z_n[x_1..x_k] modulo one monic relation per variable.

    `polys` is one coefficient list per variable, lowest degree first. The basis
    is the box of exponent vectors, a monomial sits at the mixed-radix index of
    its exponents with variable 0 fastest, and an element (a_0..a_(dim-1)) sits
    at sum(a_j n^j).

    Deliberately the most direct thing that could work: multiply monomials out
    and fold each variable's overflowing powers down on its own, using
    x^d = -(f_0 + ... + f_(d-1) x^(d-1)). No cleverness, because its job is to
    be obviously correct and to disagree with a subtly wrong implementation.
    """
    degs = [len(p) - 1 for p in polys]
    assert all(d >= 1 for d in degs), "every relation needs degree at least 1"
    assert all(p[-1] % n == 1 for p in polys), "every relation must be monic"
    k = len(polys)
    dim = 1
    for d in degs:
        dim *= d
    order = n ** dim

    def exps(idx):
        """The exponent vector of basis monomial idx."""
        e = []
        for d in degs:
            e.append(idx % d)
            idx //= d
        return e

    def mono_idx(e):
        idx = 0
        for i in reversed(range(k)):
            idx = idx * degs[i] + e[i]
        return idx

    def power(i, g):
        """x_i^g in {1, x_i, ..., x_i^(deg-1)}, by repeated folding."""
        d, p = degs[i], polys[i]
        v = [0] * max(g + 1, d)  # at least d long, so v[:d] is a full vector
        v[g] = 1
        for j in range(len(v) - 1, d - 1, -1):
            c = v[j] % n
            if c:
                v[j] = 0
                for q in range(d):
                    v[j - d + q] -= c * p[q]
        return [x % n for x in v[:d]]

    # basis[a][b] = the product of basis monomials a and b, in the basis. Each
    # variable's power expands alone, so the coefficient of monomial c is the
    # product over variables of the univariate coefficients.
    pw = [[power(i, g) for g in range(2 * degs[i] - 1)] for i in range(k)]
    basis = [[None] * dim for _ in range(dim)]
    for a in range(dim):
        ea = exps(a)
        for b in range(dim):
            eb = exps(b)
            out = [0] * dim
            for m in range(dim):
                em = exps(m)
                coef = 1
                for i in range(k):
                    coef = coef * pw[i][ea[i] + eb[i]][em[i]] % n
                out[m] = coef
            basis[a][b] = out

    def to_vec(v):
        out = []
        for _ in range(dim):
            out.append(v % n)
            v //= n
        return out

    def to_idx(v):
        idx = 0
        for i in reversed(range(dim)):
            idx = idx * n + v[i] % n
        return idx

    add = [[0] * order for _ in range(order)]
    mul = [[0] * order for _ in range(order)]
    vecs = [to_vec(v) for v in range(order)]
    for i in range(order):
        a = vecs[i]
        for j in range(order):
            b = vecs[j]
            add[i][j] = to_idx([(x + y) % n for x, y in zip(a, b)])
            acc = [0] * dim
            for p in range(dim):
                if not a[p]:
                    continue
                for q in range(dim):
                    if not b[q]:
                        continue
                    c = a[p] * b[q]
                    e = basis[p][q]
                    for m in range(dim):
                        acc[m] = (acc[m] + c * e[m]) % n
            mul[i][j] = to_idx(acc)
    # Every relation is monic of degree >= 1, so the ideal contains no nonzero
    # constant and k*1 = 0 exactly when n divides k: the characteristic is n.
    return Ring(name, order, n, 1, add, mul)


def render(n, poly, var="x"):
    """One relation as it appears in a ring name: x2+x+1, x2, x, 2y2+y+1."""
    terms = []
    for i in range(len(poly) - 1, -1, -1):
        c = poly[i] % n
        if not c:
            continue
        coef = "" if (c == 1 and i > 0) else str(c)
        sym = "" if i == 0 else (var if i == 1 else var + str(i))
        terms.append(coef + sym)
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

    print("\nReference quotient against the goldens (runs without the C++):")
    for spec, n, polys, target, why in EXPECTED:
        if target not in goldens:
            check("%-22s vs %s" % (spec, target), False, "no golden named %s" % target)
            continue
        ref = reference(n, polys, spec)
        bad = axiom_failures(ref)
        if bad:
            check("%-22s is a ring" % spec, False, "; ".join(bad))
            continue
        same = ref.key() == goldens[target].key()
        check("%-22s %s" % (spec, why), same,
              "" if same else "differs from the %s golden%s" % (target, isomorphism_hint(ref, goldens[target])))

    print("\nThe implementation, asked for each ring by --ring spec:")
    for spec, n, polys, target, why in EXPECTED:
        got = try_run(dumper, spec)
        if got is None:
            pending.append(spec)
            print("  PEND  %-22s %s -- dump_ring does not accept this spec yet" % (spec, why))
            continue
        ref = reference(n, polys, spec)
        # The name becomes EGZ_<name>.tsv, so it cannot be the spec verbatim.
        ok_name = bool(SAFE_NAME.match(got.name))
        check("%-22s has a file-safe name" % spec, ok_name,
              repr(got.name) if ok_name else "%r cannot go in EGZ_<name>.tsv" % got.name)
        ok_ref = got.key() == ref.key()
        check("%-22s matches the reference" % spec, ok_ref,
              "" if ok_ref else "differs from the reference" + isomorphism_hint(got, ref))
        gold = goldens.get(target)
        if gold is not None:
            ok_gold = got.key() == gold.key()
            check("%-22s %s" % (spec, why), ok_gold,
                  "" if ok_gold else "differs from the %s golden%s" % (target, isomorphism_hint(got, gold)))

    print("\nMultivariate rings with nothing on file to compare against:")
    for spec, n, polys, order in NEW_RINGS:
        got = try_run(dumper, spec)
        if got is None:
            pending.append(spec)
            print("  PEND  %-26s not accepted yet" % spec)
            continue
        ref = reference(n, polys, spec)
        bad = axiom_failures(got)
        check("%-26s is a commutative unit ring" % spec, not bad,
              "; ".join(bad) if bad else "order %d, char %d" % (got.order, got.characteristic))
        check("%-26s has order %d" % (spec, order), got.order == order, "got %d" % got.order)
        same = got.key() == ref.key()
        check("%-26s matches the reference" % spec, same,
              "" if same else "differs from the reference" + isomorphism_hint(got, ref))

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
