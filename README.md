# EGZ Solver
A C++ brute-force calculator for arbitrary degree Erdös-Ginzburg-Ziv constants in finite commutative unit rings.
## Supported Rings
 - $ℤ_n$, for any natural $n$.
 - $ℤ_n^m$, for any natural $n$ and $m$.
 - $𝔽_4$.
 - $\frac{ℤ_2[x]}{x^2}$
 - Products of any two of the above, via `product<R, P>` (e.g. `Z_2xZ_2`).

`--list-rings` prints the instantiations the binary was built with; see
[Adding a ring](#adding-a-ring) to expose more.

## Building
Requires a C++17 compiler. With CMake:

```sh
cmake -S . -B build
cmake --build build
```

This produces `build/egz-solver` (`build\egz-solver.exe` on Windows). Or compile
directly, since the project is a single translation unit:

```sh
g++ -std=c++17 -O2 -DDEBUG main.cpp -o egz-solver
```

`DEBUG` enables progress logging to stdout; it is on by default under CMake and
can be turned off with `-DEGZ_DEBUG=OFF`. The tables written to disk are the
same either way.

## Running
```sh
./build/egz-solver --ring Z_7 --m-max 20 --t-max 25
```

| Option | Meaning |
| --- | --- |
| `--ring NAME` | Ring to compute (default `Z_7`). |
| `--m-min N` | First `m` to compute (default 1). |
| `--m-max N` | Exclusive upper bound on `m`. |
| `--t-max N` | Exclusive upper bound on `t`. |
| `--out-dir DIR` | Where to write the table (default `Experimental tables`). |
| `--skip EXPR` | Leave rows out of the table; repeatable. See [Skipping rows](#skipping-rows). |
| `--max-work N` | Give up on a cell after `N` work units; `0` is unlimited. See [Bounding the work per cell](#bounding-the-work-per-cell). |
| `--no-file` | Print progress only; write nothing. |
| `--quiet` | Suppress per-value progress output. |
| `--list-rings` | List supported ring names and exit. |

The output directory is relative to the working directory and is created if
missing, so run from the repository root to land in `Experimental tables/`.

Be aware that cost grows steeply in both `m` and `t`: full tables for the larger
rings take hours, and a single cell deep in a table can take minutes.

## Skipping rows

Some rows cost far more time and memory than their neighbours, and `--skip`
leaves them out without recompiling. It takes an expression on `m`:

| Expression | Skips |
| --- | --- |
| `powers` | `m` that are powers of the ring's order |
| `pow:K` | `m` that are powers of `K` (counting `K^0 = 1`) |
| `mod:K=R` | everything except `m` congruent to `R` modulo `K` |
| `list:a,b,c` | exactly those `m` |
| `none` | nothing |

`--skip` may be given several times; a row is skipped if any expression says so,
so two `mod:` rules keep only the `m` satisfying both.

```sh
./build/egz-solver --ring Z_3 --skip powers      # omit m = 1, 3, 9, ...
./build/egz-solver --ring Z_2x_by_x2 --skip mod:4=1   # keep only m = 1 (mod 4)
```

Skipped rows are **omitted** from the table, not written blank, so their absence
cannot be mistaken for "no EGZ constant exists". The `m` that were skipped are
reported on stderr.

For $ℤ_n$, `--skip powers` leaves out exactly the `m = n^k` rows, which are both
the most expensive and the ones already settled in closed form (see
[tests/README.md](tests/README.md)), so little is lost by skipping them.

## Bounding the work per cell

`--skip` predicts which rows are expensive. `--max-work N` measures instead:
each `EGZ(t, m)` is given `N` units of work, and a cell that exceeds its budget
is abandoned. This is the option to reach for when you do not yet know where the
cost lies -- exploring `m` that are not prime powers, say.

```sh
./build/egz-solver --ring F_4 --max-work 200000
```

A unit is one step of the search that had to recurse: an `e_m` evaluation that
missed the memo, or a call into the counterexample enumeration. Memo hits are
free, so the count tracks work actually done rather than cells visited. The
budget is per cell, so one abandoned cell does not penalise the next, and
whatever was memoised before an abort stays available.

Abandoned cells are written as `?` and reported on stderr. They are never left
blank, which would be indistinguishable from "no EGZ constant exists". Here
`--max-work 400` gave up on the five cells at the right-hand end of rows 3 and
4; the `?` left of the diagonal are the ordinary never-computed ones.

```
        1       2       3       4       5       6       7       8       9       10      11
1                       2                       2                       2
2       ?               3       3               3       3               3       3
3       ?       ?                                                       ?       ?       ?
4       ?       ?       ?                       2                       2       ?       ?
```

A budget only costs you answers, it never changes them: any cell that finishes
under a budget holds exactly the value an unbudgeted run produces. Raising the
budget can only turn `?` into a value, never alter one.

## Output format
One `.tsv` per ring, named `EGZ_<ring>.tsv`. **Rows are `m`, columns are `t`, and
a cell holds `EGZ(t, m) - t`, not `EGZ(t, m)` itself.** The first row is the `t`
header; the first column is `m`.

So a cell containing `6` at row `m = 1`, column `t = 21` means `EGZ(21, 1) = 27`.

A blank cell means the solver computed that `(t, m)` and got that `EGZ(t, m)` is infinite/no EGZ constant exists there. A blank is an answer.

**A `?` means no value was computed**, for one of three reasons:
 - `t < T_MIN(m)`, i.e. `t < m` — left of the diagonal, outside the row's range
   of `t`.
 - `t >= T_MAX(m)` — beyond the bound the row was generated with.
 - the search was abandoned under `--max-work`; see
   [Bounding the work per cell](#bounding-the-work-per-cell).

The first two are fixed by `(t, m)` and the row's bounds, so they are easy to
tell from an abandoned cell, but the format does not distinguish the three.

A row left out by `--skip` is absent from the file entirely. An omitted row, a
`?` and a blank are therefore three distinguishable things.

Within a row's computed range, a $ℤ_n$ cell holds a value exactly when
$\binom{t}{m} \equiv 0 \pmod n$ and is blank otherwise, which is a useful sanity
check on a generated table.

All 6,125 such cells across the committed $ℤ_n$ tables satisfy that criterion;
`tests/verify_against_thesis.py` checks it, along with the rule that everything
outside a computed range carries `?`.

Note that `EGZ_Z_2x_by_x2.tsv` was generated with a per-row bound on `t` --
`smallestPowerBiggerThan(2, m) + m + 1`, the form that survives commented out in
`config.hpp` -- rather than the flat bound the other eight tables use. Its rows
therefore run out of values well short of the header width, and the `?` that
follow mean "not computed", not "no EGZ constant".

## Search bounds
`--m-max` and `--t-max` are not merely loop limits.

`m` indexes the solver's per-degree memo tables, so `m >= M_MAX()` is out of
bounds; `EGZSolver::EGZ` rejects it rather than corrupting memory. Before this
was guarded, computing a table with rows past the compiled bound segfaulted.

`--t-max` doubles as the radix in which `sequence::identifier()` packs element
multiplicities, so multiplicities at or above it collide. That is now a
performance concern only, not a correctness one: `sequence::operator==` compares
multiplicities directly, so a collision costs an extra bucket probe instead of
returning another sequence's memoized value.

The compiled-in defaults can be changed with `-DEGZ_M_MAX=` / `-DEGZ_T_MAX=`;
the CLI flags override them at run time.

## Tests
```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Two suites run:

 - `regression` replays 164 known-good values sampled from `Experimental tables/`
   across all nine tables.
 - `thesis_verification` checks the committed tables against the theorems proved
   in the accompanying thesis, covering roughly 12,000 cells.

See [tests/README.md](tests/README.md).

## Source layout
| File | Contents |
| --- | --- |
| `main.cpp` | CLI and the table writer. |
| `egz_solver.hpp` | The search itself: `e_m`, counterexample enumeration, `EGZ(t, m)`. |
| `rings.hpp` | Ring implementations (`Zn`, `Znp`, `F4`, `Z_2_over`, `product`). |
| `ring_registry.hpp` | Maps a `--ring` name to its type. |
| `sequence.hpp` | Multiset of ring elements, with hashing for memoization. |
| `config.hpp` | Search bounds (`M_MAX`, `T_MIN`, `T_MAX`). |
| `skip_rule.hpp` | Parses and applies the `--skip` expressions. |
| `conditional_file_stream.hpp` | Output stream that can be switched off. |

Everything but `main.cpp` is a header, so the project builds as one translation
unit. Each header is self-contained and guarded with `#pragma once`.

## Adding a ring
1. Implement the ring in `rings.hpp`. Use `class ring` as the checklist of what
   is required: a `static constexpr int characteristic`, `order`, and `unit`, a
   constructor from `int`, `operator+`, `operator*`, and a `static string name()`.
   Elements are identified with the integers `0 .. order - 1`, and `value` must
   agree with that indexing.
2. Add the type to `AllRings` in `ring_registry.hpp`. It is then selectable by
   its `name()` via `--ring`.
Rows are left out with `--skip` at run time, so a ring needs no hook for that.

## License

The code is under the [MIT License](LICENSE); the computed tables in
`Experimental tables/` are under
[CC BY 4.0](Experimental%20tables/LICENSE). Copyright (c) 2026 Iuri Grangeiro
Carvalho.

The split is deliberate: Creative Commons licenses are not intended for
software, and a software license sits awkwardly on what is essentially
mathematical data. In practice this means you may use the solver freely with
attribution preserved in the source, and reuse or republish the tables provided
you give credit. See [Experimental tables/README.md](Experimental%20tables/README.md)
for a suggested citation.
