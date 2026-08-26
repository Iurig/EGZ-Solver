# EGZ Solver
A C++ brute-force calculator for arbitrary degree Erdös-Ginzburg-Ziv constants in finite commutative unit rings.
## Supported Rings
 - $ℤ_n$, for any natural $n$.
 - $ℤ_n^m$, for any natural $n$ and $m$.
 - $𝔽_4$.
 - $\frac{ℤ_2[x]}{x^2}$
 - Products of any two of the above, via `product<R, P>` (e.g. `Z_2xZ_2`).
 - $ℤ_n[x_1 \dots x_k]$ modulo one relation per variable, named directly on the
   command line. See [Runtime rings](#runtime-rings).

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
./build/egz-solver --ring Z_2 --m-max 20 --t-max 25
```

| Option | Meaning |
| --- | --- |
| `--ring NAME` | Ring to compute (default `Z_2`). |
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

## Runtime rings

Besides the rings listed above, `--ring` takes a quotient of a polynomial ring
written out directly, so you can compute one without recompiling:

```sh
./build/egz-solver --ring 'Z_2[x]/(x^2+x+1)'
./build/egz-solver --ring 'Z_4[x]/(x^2+1)'
./build/egz-solver --ring 'Z_2[x,y]/(x^2,y^2)'
./build/egz-solver --ring Z_2xy_by_x2_and_y2   # the same ring, no quoting needed
```

Each variable needs exactly one relation, in that variable alone. A relation
needs degree at least 1 and a leading coefficient invertible mod `n`; a zero
divisor there leaves a ring the solver cannot compute in, and it says so rather
than guessing. Coefficients may be written with `-`, and `x^2` and `x2` are both
accepted.

A relation tying variables together — `(xy-1)`, `(x^2-y)` — is rejected rather
than approximated. Those need a Gröbner basis, which this does not have.

The ring has `n^(d1 * ... * dk)` elements, for relation degrees `d1 .. dk`, and
that is the number to watch: cost climbs steeply with it, and anything past 256
elements is refused as unsearchable. `Z_2[x,y]/(x^2,y^2)` is already 16.

The table lands in `EGZ_<ring name>.tsv`, and that name is itself a valid
`--ring` argument, so a file name can always be fed back to reproduce it.

`Z_2[x]/(x^2)` and `Z_2[x]/(x^2+x+1)` are the built-in `Z_2x_by_x2` and `F_4`
written the other way round. They give identical results, down to byte-identical
EGZ tables, which `tests/test_rings.py` checks; a name matching a built-in still
selects the built-in, since the published tables were computed with it.

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

## Two searches

There are two implementations of `EGZ(t, m)`, and `--method` picks between them.
They agree everywhere they have been compared; the reason to have both is that
each one checks the other, and they are fast in different places.

```sh
./build/egz-solver --ring Z_7 --method bottom-up
```

**Top-down** (the default, `egz_solver.hpp`) takes each candidate length `l` in
turn and searches for a counterexample: a sequence of length `l` with no
zero-`e_m` subsequence of length `t`. The first `l` with no counterexample is the
answer. It can stop the moment it finds one, and often does.

**Bottom-up** (`egz_bottom_up.hpp`) works the other way. It finds every multiset
of size `t` with `e_m = 0`, then every multiset of size `t + 1` containing one of
those, and so on, stopping at the first size where every multiset is covered.
Each level is one flat pass, because for `|S| > t` a multiset is covered exactly
when `S - x` is covered for some `x` in it — no search and no backtracking.

Only *which* `e_m` are zero ever matters to it: the values are computed once at
level `t` and never looked at again.

That first step is also where its time goes. Deciding which `e_m` vanish takes
61–72% of the total across the rings measured, despite touching roughly a tenth
as many multisets as the climb above it — a multiset costs around 270 ns at
level `t` against 13 ns on the way up. The climb is nearly free; the `e_m`
evaluations are the bottleneck, and they are the part worth optimising.

The trade is that bottom-up visits every multiset of every level, where top-down
can stop early — but it never re-derives anything, and top-down re-derives a
great deal, since each candidate length starts a fresh search over ground the
last one covered.

Sweeping `m < 12`, `t < 24`, every cell agreeing, with
`tests/compare_methods.cpp`:

| Ring | Time, top-down | Time, bottom-up | `e_m` memo, top-down | memo, bottom-up | Peak levels |
| --- | --- | --- | --- | --- | --- |
| `Z_3` | 3 ms | 2 ms | 2.8k entries | 3.0k | 435 multisets |
| `F_4` | 89 ms | 35 ms | 24.4k | 25.7k | 7.1k |
| `Z_2^2` | 307 ms | 45 ms | 24.4k | 25.7k | 11.5k |
| `Z_5` | 368 ms | 140 ms | 134k | 140k | 36k |
| `Z_7` | 77.1 s | 10.0 s | 4.27M | 4.43M | 3.26M |

**Time** goes to bottom-up everywhere measured, by more as the ring grows: 1.5×
on `Z_3`, 7.7× on `Z_7`. Per cell the spread is wider than the totals suggest —
the worst `Z_7` cells run 5.0 s against 0.35 s, a 14× gap — and across 198 `Z_7`
cells there was **no cell where bottom-up was slower**.

The reason is that top-down re-derives. Each candidate length starts a fresh
counterexample search over ground the previous one covered, so a cell answering
at `l = t + 9` has run nine overlapping searches; bottom-up builds each level
once from the one below.

**Memory is close to a tie**, which is not what the descriptions suggest. Both
are dominated by the memo of `e_m` results — around 375 MiB of it on that `Z_7`
sweep — and bottom-up's is 3-5% larger, because it evaluates `e_m` on every
multiset of size `t` rather than only the ones a search happens to reach. The
levels it additionally holds are two at a time at one bit per multiset: **796 KiB
against 375 MiB** on `Z_7`. The sweep is the cheap part.

The level term is still the one with a ceiling, though. A level of size `l` over
a ring of order `k` holds `C(l + k - 1, k - 1)` multisets, growing fast in both,
so a large enough ring with a large enough answer does eventually become
level-dominated. Past `EGZ_MAX_LEVEL` bottom-up returns `?` rather than trying
to allocate; top-down has no such ceiling.

That cap is worth knowing about, because setting it too tight throws away cells
the method could otherwise do. `EGZ(12, Z_12, 1)` is the classical
Erdős-Ginzburg-Ziv case, answer `2n - 1 = 23`. Its top level holds 548 million
multisets, so a 40M cap abandons it in 3 seconds while a cap that admits it
solves it in **17.5 s using 282 MB** — and top-down, on the same cell, reaches
`l = 23` quickly but had not finished proving there is no counterexample there
after two minutes. The default cap is set high for that reason; lower it with
`-DEGZ_MAX_LEVEL=` if the memory matters more than the reach.

So the honest summary is that bottom-up won every cell where both finished, and
the one cell found where it gives up is one top-down could not do either.

Neither is a wrapper around the other: they have separate `e_m` implementations
and separate memos, sharing only `sequence` and the ring. That is what makes
their agreement worth something — see
[Two searches, checked against each other](tests/README.md).

## Output format
One `.tsv` per ring, named `EGZ_<ring>.tsv`. **Rows are `m`, columns are `t`, and
a cell holds `EGZ(t, m) - t`, not `EGZ(t, m)` itself.** The first row is the `t`
header; the first column is `m`.

So a cell containing `6` at row `m = 1`, column `t = 21` means `EGZ(21, 1) = 27`.

A blank cell means the solver computed that `(t, m)` and got that `EGZ(t, m)` is
infinite/no EGZ constant exists there. A blank is an answer. However, **a `?`
means no value was computed**, for one of three reasons:
 - `t < m` — left of the diagonal, outside the row's range of `t`.
 - `t` past the upper bound the row was generated with.
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
`--t-max` sets the last column computed, but it also has to exceed every element
multiplicity the search reaches — not just the largest `t` you want. A search
that reaches 30 copies of one element wants `--t-max 31`, whatever `t` is. Too
low on that count only costs speed, never correctness.

`--m-max` is the harder bound: a row past it is refused outright rather than
computed, so raise it before asking for one.

Both have compiled-in defaults, changeable with `-DEGZ_M_MAX=` / `-DEGZ_T_MAX=`,
and the CLI flags override them at run time.

## Tests
```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Eight suites run, in a few seconds:

 - `regression` and `regression_bottom_up` replay 176 known-good values sampled
   from `Experimental tables/` across all nine tables — once with each of the
   [two searches](#two-searches).
 - `methods_agree` and `methods_agree_f4` run both searches over every cell of a
   small sweep and fail on any disagreement.
 - `thesis_verification` checks the committed tables against the theorems proved
   in the accompanying thesis.
 - `rings` checks the rings themselves: ring axioms, and that each quotient
   reproduces the built-in ring it generalises.
 - `skip_rules` and `max_work` drive those two options through the CLI.

See [tests/README.md](tests/README.md).

## Source layout
| File | Contents |
| --- | --- |
| `main.cpp` | CLI and the table writer. |
| `egz_solver.hpp` | The top-down search: `e_m`, counterexample enumeration, `EGZ(t, m)`. |
| `egz_bottom_up.hpp` | The bottom-up search, an independent second implementation. |
| `rings.hpp` | Ring implementations (`Zn`, `Znp`, `F4`, `Z_2_over`, `product`). |
| `quotient.hpp` | Parses and builds the `Z_n[x]/(P)` rings named at run time. |
| `ring_registry.hpp` | Maps a `--ring` name or spec to its ring. |
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
3. Regenerate `tests/ring_goldens.tsv` with `./build/dump_ring --all > …`, so the
   new ring is covered by the axiom and stability checks in `tests/test_rings.py`.

Rows are left out with `--skip` at run time, so a ring needs no hook for that.

A quotient of a polynomial ring needs none of this — see
[Runtime rings](#runtime-rings). Relations mixing variables would mean a Gröbner
basis in `buildBasis()`; the rest of `quotient.hpp` is written not to care,
since it only ever asks for the basis and the products of basis monomials.

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
