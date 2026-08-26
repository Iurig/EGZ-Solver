#!/usr/bin/env python3
"""Extend the published tables in Experimental tables/ to larger m and t.

    python tests/expand_tables.py --solver build/egz-solver --add-rows 4 --add-cols 4

Each table is recomputed with wider bounds and merged back. The merge has one
rule that matters:

    a value already published is never overwritten.

A new run can fill a cell that had no value, never change one. A recomputed cell
that disagrees with a published one is a regression: the script stops and says
so rather than quietly rewriting data others may have cited. So a run is also a
full replay of everything committed -- the widest regression check here, at the
cost of recomputing it.

Cells the run gives up on come back as `?`. A published value stays; where there
was none, the `?` stands and says the cell is still open.

Options:
  --solver PATH     egz-solver binary (required)
  --tables DIR      directory of EGZ_*.tsv (default: Experimental tables/)
  --ring NAME       expand only this table; repeatable
  --add-rows N      how many more m to attempt (default 4)
  --add-cols N      how many more t to attempt (default 4)
  --max-work N      per-cell work budget passed through (default 0, unlimited)
  --method WHICH    top-down or bottom-up (default: the solver's own default)
  --timeout SECONDS per-ring wall clock limit. Rows finished before it are still
                    merged; the row in progress is dropped
  --dry-run         report what would change, write nothing
"""

import argparse
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time

ABANDONED = "?"


def read_table(path):
    """(header_t_values, {m: {t: text}}), text '' for blank; (None, None) when
    there is not even a header -- what a run killed early can leave behind."""
    raw = io.open(path, "rb").read().decode("utf-8")
    lines = raw.splitlines()
    if not lines or not lines[0].startswith("\t"):
        return None, None
    header = lines[0].split("\t")
    ts = [int(x) for x in header[1:]]
    rows = {}
    for line in lines[1:]:
        f = line.split("\t")
        if not f or not f[0]:
            continue
        rows[int(f[0])] = {int(header[i]): f[i] for i in range(1, len(f))}
    return ts, rows


def write_table(path, ts, rows):
    """Writes in the committed layout: newline before each row, none trailing."""
    out = ["\t" + "\t".join(str(t) for t in ts)]
    for m in sorted(rows):
        cells = [str(m)] + [rows[m].get(t, ABANDONED) for t in ts]
        out.append("\t".join(cells))
    io.open(path, "wb").write("\r\n".join(out).encode("utf-8"))


def merge(old_rows, new_rows, ts, name, problems):
    """Published values win; new values only fill cells that had none."""
    merged, gained, kept, still_open, outside = {}, 0, 0, 0, 0
    for m in sorted(set(old_rows) | set(new_rows)):
        old, new = old_rows.get(m, {}), new_rows.get(m, {})
        row = {}
        for t in ts:
            if t < m:
                # Left of the diagonal: never computed by convention, not open.
                row[t] = ABANDONED
                outside += 1
                continue
            o, n = old.get(t), new.get(t)
            has_o = o is not None and o != ABANDONED
            has_n = n is not None and n != ABANDONED
            if has_o and has_n and o != n:
                # A published value and a fresh one disagree. One is wrong, and
                # it is not for a script to decide which.
                problems.append("%s m=%d t=%d: published %r, recomputed %r" % (name, m, t, o or "blank", n or "blank"))
            if has_o:
                row[t] = o
                kept += 1
            elif has_n:
                row[t] = n
                gained += 1
            else:
                row[t] = ABANDONED
                still_open += 1
        merged[m] = row
    return merged, gained, kept, still_open, outside


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--solver", required=True)
    ap.add_argument("--tables", default=os.path.join(os.path.dirname(here), "Experimental tables"))
    ap.add_argument("--ring", action="append", default=[])
    ap.add_argument("--add-rows", type=int, default=4)
    ap.add_argument("--add-cols", type=int, default=4)
    ap.add_argument("--max-work", type=int, default=0)
    ap.add_argument("--method")
    ap.add_argument("--timeout", type=float)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    solver = os.path.abspath(args.solver)
    if not os.path.exists(solver) and os.path.exists(solver + ".exe"):
        solver += ".exe"
    if not os.path.exists(solver):
        print("no such binary: %s" % solver, file=sys.stderr)
        return 2

    names = sorted(f for f in os.listdir(args.tables) if f.startswith("EGZ_") and f.endswith(".tsv"))
    if args.ring:
        wanted = {"EGZ_%s.tsv" % r for r in args.ring}
        names = [n for n in names if n in wanted]
    if not names:
        print("no tables selected", file=sys.stderr)
        return 2

    problems, failures = [], 0
    for name in names:
        path = os.path.join(args.tables, name)
        ring = name[len("EGZ_"):-len(".tsv")]
        ts, old_rows = read_table(path)
        old_t_max, old_m_max = max(ts) + 1, max(old_rows) + 1
        t_max, m_max = old_t_max + args.add_cols, old_m_max + args.add_rows

        print("%-22s t<%d -> t<%d, m<%d -> m<%d" % (ring, old_t_max, t_max, old_m_max, m_max), flush=True)
        cmd = [solver, "--ring", ring, "--m-max", str(m_max), "--t-max", str(t_max), "--quiet"]
        if args.max_work:
            cmd += ["--max-work", str(args.max_work)]
        if args.method:
            cmd += ["--method", args.method]

        tmp = tempfile.mkdtemp()
        produced = os.path.join(tmp, name)
        started, partial = time.time(), False
        try:
            cmd += ["--out-dir", tmp]
            try:
                proc = subprocess.run(cmd, capture_output=True, timeout=args.timeout)
                if proc.returncode != 0:
                    print("    solver exited %d: %s" % (proc.returncode, proc.stderr.decode(errors="replace")[:200]))
                    failures += 1
                    continue
            except subprocess.TimeoutExpired:
                # The solver flushes each row, so a timeout still leaves what it
                # finished on disk -- which is what makes a long run worth
                # starting. The row in progress is dropped: half a row would
                # merge in as blanks, and blanks are answers.
                partial = True
            elapsed = time.time() - started
            if not os.path.exists(produced):
                print("    %.0fs: nothing written%s" % (elapsed, " before the timeout" if partial else ""))
                failures += 1
                continue
            new_ts, new_rows = read_table(produced)
            if new_ts is None:
                print("    %.0fs: nothing usable written%s" % (elapsed, " before the timeout" if partial else ""))
                failures += 1
                continue
            if partial and new_rows:
                new_rows.pop(max(new_rows), None)
            merged, gained, kept, still_open, outside = merge(old_rows, new_rows, new_ts, ring, problems)
            print("    %.0fs%s: %d gained, %d kept, %d still open, %d outside the diagonal"
                  % (elapsed, " (timed out, partial)" if partial else "", gained, kept, still_open, outside), flush=True)
            if partial:
                failures += 1
            if not args.dry_run and not problems:
                write_table(path, new_ts, merged)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    if problems:
        print("\nRECOMPUTED VALUES DISAGREE WITH PUBLISHED ONES -- nothing written:")
        for p in problems[:20]:
            print("  " + p)
        return 1
    if failures:
        print("\n%d table(s) could not be expanded" % failures)
    return 0


if __name__ == "__main__":
    sys.exit(main())
