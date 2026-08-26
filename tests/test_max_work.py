#!/usr/bin/env python3
"""Exercise --max-work end to end through the CLI.

    python tests/test_max_work.py path/to/egz-solver

The property that matters is that a budget only ever costs you answers, never
changes them: a cell the solver finishes under a budget must equal the cell it
produces with no budget at all. Abandoning a cell mid-recursion leaves shared
memo tables and a mutated sequence behind, so getting that wrong would corrupt
later cells rather than fail loudly.
"""

import io
import os
import subprocess
import sys
import tempfile

RING = "Z_3"
M_MAX = "8"
T_MAX = "16"
ABANDONED = "?"
BUDGETS = [100, 500, 2000, 10000]

failures = []


def check(name, cond, detail=""):
    print("  %s  %s%s" % ("PASS" if cond else "FAIL", name, "  " + detail if detail else ""))
    if not cond:
        failures.append(name)


def run(solver, out_dir, budget=None):
    cmd = [solver, "--ring", RING, "--m-max", M_MAX, "--t-max", T_MAX, "--quiet", "--out-dir", out_dir]
    if budget is not None:
        cmd += ["--max-work", str(budget)]
    proc = subprocess.run(cmd, capture_output=True, timeout=600)
    path = os.path.join(out_dir, "EGZ_%s.tsv" % RING)
    raw = io.open(path, "rb").read().decode("utf-8")
    cells = {}
    for line in raw.splitlines()[1:]:
        f = line.split("\t")
        if not f or not f[0]:
            continue
        for t in range(1, len(f)):
            if f[t]:
                cells[(int(f[0]), t)] = f[t]
    return proc, cells


def main():
    if len(sys.argv) < 2:
        print("usage: test_max_work.py <path-to-egz-solver>", file=sys.stderr)
        return 2
    # Windows CreateProcess rejects a relative path written with forward slashes.
    solver = os.path.abspath(sys.argv[1])
    if not os.path.exists(solver):
        print("no such binary: %s" % solver, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        _, base = run(solver, tmp)
        check("unbudgeted run has no abandoned cells", ABANDONED not in base.values())
        check("unbudgeted run produced values", len(base) > 0, "%d cells" % len(base))

        # --max-work 0 means no limit.
        _, zero = run(solver, tmp, 0)
        check("--max-work 0 means unlimited", zero == base)

        solved_counts = []
        for b in BUDGETS:
            proc, cells = run(solver, tmp, b)
            solved = {k: v for k, v in cells.items() if v != ABANDONED}
            given_up = [k for k, v in cells.items() if v == ABANDONED]
            solved_counts.append(len(solved))

            wrong = [(k, v, base.get(k)) for k, v in solved.items() if base.get(k) != v]
            check("budget %d: completed cells match the unbudgeted run" % b, not wrong,
                  str(wrong[:2]) if wrong else "%d solved" % len(solved))
            # A budget must not invent cells the unbudgeted run left blank, nor
            # lose one entirely: every coordinate is still accounted for.
            check("budget %d: no spurious cells" % b, all(k in base for k in solved))
            check("budget %d: nothing silently dropped" % b, all(k in cells for k in base))
            if given_up:
                check("budget %d: reports abandoned count" % b, b"abandoned" in proc.stderr)

        check("more budget never solves fewer cells", solved_counts == sorted(solved_counts), str(solved_counts))
        check("a tight budget does abandon something", solved_counts[0] < len(base),
              "%d of %d" % (solved_counts[0], len(base)))

    print()
    if failures:
        print("%d check(s) failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
