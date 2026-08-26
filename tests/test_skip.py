#!/usr/bin/env python3
"""Exercise the --skip expressions end to end through the CLI.

    python tests/test_skip.py path/to/egz-solver

Checks which rows reach the table, that skipping omits rows rather than blanking
them, and that malformed expressions are rejected. Uses Z_3 with small bounds so
the whole suite runs in a couple of seconds.
"""

import io
import os
import subprocess
import sys
import tempfile

RING = "Z_3"
M_MAX = "12"
T_MAX = "20"


def run(solver, out_dir, *skips):
    cmd = [solver, "--ring", RING, "--m-max", M_MAX, "--t-max", T_MAX,
           "--quiet", "--out-dir", out_dir]
    for s in skips:
        cmd += ["--skip", s]
    return subprocess.run(cmd, capture_output=True, timeout=300)


def rows(out_dir):
    """Row labels present in the table, and the file's raw text."""
    path = os.path.join(out_dir, "EGZ_%s.tsv" % RING)
    raw = io.open(path, "rb").read().decode("utf-8")
    lines = raw.splitlines()
    return [l.split("\t")[0] for l in lines[1:]], raw


failures = []


def check(name, cond, detail=""):
    print("  %s  %s%s" % ("PASS" if cond else "FAIL", name, "  " + detail if detail else ""))
    if not cond:
        failures.append(name)


def main():
    if len(sys.argv) < 2:
        print("usage: test_skip.py <path-to-egz-solver>", file=sys.stderr)
        return 2
    # Windows CreateProcess rejects a relative path written with forward slashes.
    solver = os.path.abspath(sys.argv[1])
    if not os.path.exists(solver):
        print("no such binary: %s" % solver, file=sys.stderr)
        return 2

    all_m = [str(m) for m in range(1, 12)]
    with tempfile.TemporaryDirectory() as tmp:
        p = run(solver, tmp)
        check("no --skip keeps every row", p.returncode == 0 and rows(tmp)[0] == all_m)

        # Z_3 has order 3, so "powers" means 1, 3, 9.
        p = run(solver, tmp, "powers")
        got, raw = rows(tmp)
        check("powers omits 1, 3, 9", got == ["2", "4", "5", "6", "7", "8", "10", "11"], str(got))
        check("skipped rows are reported on stderr", b"skipped 3 rows" in p.stderr)
        check("no blank rows left behind", all(l.strip() for l in raw.splitlines()))
        widths = set(len(l.split("\t")) for l in raw.splitlines())
        check("field count stays uniform", len(widths) == 1, str(widths))
        check("no trailing newline", not raw.endswith("\n"))

        p = run(solver, tmp, "pow:2")
        check("pow:2 omits 1, 2, 4, 8", rows(tmp)[0] == ["3", "5", "6", "7", "9", "10", "11"])

        p = run(solver, tmp, "mod:4=1")
        check("mod:4=1 keeps only m = 1 (mod 4)", rows(tmp)[0] == ["1", "5", "9"])

        p = run(solver, tmp, "list:2,5")
        check("list:2,5 omits exactly those", rows(tmp)[0] == ["1", "3", "4", "6", "7", "8", "9", "10", "11"])

        # Several expressions union their skips.
        p = run(solver, tmp, "list:2,5", "pow:2")
        check("repeated --skip unions the rules", rows(tmp)[0] == ["3", "6", "7", "9", "10", "11"], str(rows(tmp)[0]))

        p = run(solver, tmp, "none")
        check("none keeps every row", rows(tmp)[0] == all_m)

        for bad in ["bogus", "pow:1", "mod:4=9", "mod:4", "list:1,x"]:
            p = run(solver, tmp, bad)
            check("rejects --skip %s" % bad, p.returncode == 2 and b"--skip" in p.stderr)

    print()
    if failures:
        print("%d check(s) failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
