#!/usr/bin/env python3
"""Exercise --memo-cap end to end through the CLI.

    python tests/test_memo_cap.py path/to/egz-solver

Eviction must cost time and nothing else: a capped run produces the table an
uncapped run produces, under either search, whether the cap was written in
entries or in bytes.
"""

import io
import os
import subprocess
import sys
import tempfile

RING = "Z_3"
M_MAX = "7"
T_MAX = "14"
CAPS = [1, 16, 500, 100000]
# The same flag read as bytes; the smallest of these buys well under one entry.
BYTE_CAPS = ["1K", "64KiB", "4MB", "1G"]

failures = []


def check(name, cond, detail=""):
    print("  %s  %s%s" % ("PASS" if cond else "FAIL", name, "  " + detail if detail else ""))
    if not cond:
        failures.append(name)


def run(solver, out_dir, method, cap=None):
    cmd = [solver, "--ring", RING, "--m-max", M_MAX, "--t-max", T_MAX, "--method", method,
           "--quiet", "--out-dir", out_dir]
    if cap is not None:
        cmd += ["--memo-cap", str(cap)]
    proc = subprocess.run(cmd, capture_output=True, timeout=600)
    path = os.path.join(out_dir, "EGZ_%s.tsv" % RING)
    return proc, io.open(path, "rb").read().decode("utf-8")


def rejects(solver, value):
    proc = subprocess.run([solver, "--memo-cap", value], capture_output=True, timeout=60)
    return proc.returncode == 2 and b"--memo-cap" in proc.stderr


def main():
    if len(sys.argv) < 2:
        print("usage: test_memo_cap.py <path-to-egz-solver>", file=sys.stderr)
        return 2
    # Windows CreateProcess rejects a relative path written with forward slashes.
    solver = os.path.abspath(sys.argv[1])
    if not os.path.exists(solver):
        print("no such binary: %s" % solver, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        tables = {}
        for method in ("bottom-up", "top-down"):
            _, base = run(solver, tmp, method)
            tables[method] = base
            check("%s: uncapped run produced a table" % method, len(base) > 0, "%d bytes" % len(base))

            # --memo-cap 0 means no limit.
            _, zero = run(solver, tmp, method, 0)
            check("%s: --memo-cap 0 means unlimited" % method, zero == base)

            for cap in CAPS + BYTE_CAPS:
                _, capped = run(solver, tmp, method, cap)
                check("%s: cap %s matches the uncapped run" % (method, cap), capped == base)

        check("both searches agree under a cap", tables["bottom-up"] == tables["top-down"])

        check("rejects a negative cap", rejects(solver, "-1"))
        check("rejects a non-numeric cap", rejects(solver, "abc"))
        check("rejects an unknown unit", rejects(solver, "8X"))
        check("rejects a byte cap that overflows", rejects(solver, "18446744073709551615K"))

    print()
    if failures:
        print("%d check(s) failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
