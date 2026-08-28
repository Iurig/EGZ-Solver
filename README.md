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
| `--memo-cap N` | Hold at most `N` memoized `e_m` values, dropping one not read lately; `0` is unlimited, and the default is `8G`. A `K`/`M`/`G`/`T` suffix reads `N` as bytes instead. See [Bounding the memo](#bounding-the-memo). |
| `--no-file` | Print progress only; write nothing. |
| `--quiet` | Suppress per-value progress output. |
| `--method WHICH` | `bottom-up` (default) or `top-down`. See [How the search works](#how-the-search-works). |
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
divisor there leaves a ring the solver cannot compute in, which it will warn about.
Coefficients may be written with `-`, and `x^2` and `x2` are both accepted.

A relation tying variables together — `(xy-1)`, `(x^2-y)` — won't work. Those need
a Gröbner basis, which this does not have.

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

## Bounding the memo

Both searches memoize every `e_m` they evaluate, and the reuse is most of the
speed. On a large ring that memo is where the memory goes, so it is capped at
**8 GiB by default** -- once it holds that many entries, each new entry evicts
one that has not been read lately. `--memo-cap 0` lifts the cap entirely, which
is worth knowing before running one: nothing else bounds the memo but the ring,
and a large sweep will take every byte the machine has and then page.

Eviction is second-chance rather than exact LRU: a hit sets a bit on the entry,
and a hand sweeping the entries evicts the first it finds with the bit clear,
clearing the bits it passes. That costs one byte per hit where an exact order
cost a linked-list splice, and on a `Z_7` sweep held to 200,000 entries it
matched exact LRU's hit rate to within a tenth of a percent while running 1.8x
faster.

```sh
./build/egz-solver --ring F_4 --memo-cap 20000000
```

A bare `N` counts entries. With a `K`, `M`, `G` or `T` suffix it is bytes
instead -- binary, and `KB` and `KiB` both mean 1024 -- which the solver divides
by what one entry costs on the ring in hand:

```sh
./build/egz-solver --ring F_4 --memo-cap 8G
```

That division is close but not exact. Every entry really is the same size, since
a `sequence` always holds `order` ints, but what the allocator adds per block is
a guess, calibrated by measuring a full memo against its cap. Buckets are the
term that matters -- an `unordered_map` holds one per entry and MSVC spends two
list iterators on each -- which is why the memo is a single table keyed on `m`
and the sequence together rather than one table per `m`. Per-degree tables
ratchet: each grows its bucket array in turn and keeps it after eviction empties
it, reaching 16x the entry count on a `Z_6` sweep and costing gigabytes that no
byte cap accounted for. Treat a byte cap as a target the memo aims at, not a ceiling the process
cannot cross, as the ring tables and the runtime are outside it.

Eviction never changes an answer -- a miss recomputes the value -- so a capped
run produces the same table as an uncapped one, only slower. The two budgets do
interact, though: a recomputed value is charged to `--max-work` again, so a
tight cap and a tight budget together can abandon a cell that either alone would
finish.

The cap is one number spanning every `m` rather than one per degree, and it is
read once, when the solver is built. There is a compiled-in default too,
`-DEGZ_MEMO_CAP=`, which the flag overrides at run time.

## How the search works

`EGZ(t, m)` is computed level by level. The solver finds every multiset of size
`t` with `e_m = 0`, then recurses, finding every multiset of size `t + i + 1` 
containing a multiset of size `t + i`, and stops at the first size where every 
multiset is covered. That size is the `EGZ` in question.

Each level is a single flat pass with no search and no backtracking, because 
which `S` have `e_m(S) = 0` is a function only of *which* `e_m(S’)` are zero in
sizes one smaller than `S`, so the values are computed once at level `t` and
never looked at again.

Worth knowing before running anything large:

**Memory has a ceiling.** Two levels are live at a time, one bit per multiset,
and a level of size `l` over a ring of order `k` holds `C(l + k - 1, k - 1)` of
them. Past `EGZ_MAX_LEVEL` the solver writes `?` rather than trying to allocate.
The default admits about 500 MB per level, so 1 GB for the two live at once,
which is what an order-9 ring needs: its levels pass 200 MB by level 48, and a
ceiling that stops there turns computable cells into `?`. The rest is allocated 
for `e_m`. This is editable.

For most rings none of this binds: on a `Z_7` sweep the levels came to 796 KiB
against 375 MiB of `e_m` memo, so the memo is what you actually pay for.

### The other search

`--method top-down` selects a second, independent implementation
(`egz_top_down.hpp`), which was the one used in the original thesis. It takes
each candidate length `l` in turn and searches for a counterexample — a 
sequence of length `l` with no zero-`e_m` subsequence of length `t` — and 
returns the first `l` that has none.

It still exists because it is still useful for generating tests, as they share
only `sequence` and the ring; the `e_m` recurrence and the memo are written 
separately in each, and [the test suite](tests/README.md) holds them to 
producing identical tables.

It is also slower, because each candidate length starts a fresh search over
ground the previous one covered. Sweeping `m < 12`, `t < 24` with
`tests/compare_methods.cpp`, every cell agreeing:

| Ring | Top-down | Bottom-up |
| --- | --- | --- |
| `Z_3` | 3 ms | 2 ms |
| `F_4` | 89 ms | 35 ms |
| `Z_2^2` | 307 ms | 45 ms |
| `Z_5` | 368 ms | 140 ms |
| `Z_7` | 77.1 s | 10.0 s |

Across 198 `Z_7` cells there was no cell where bottom-up was slower, and the
worst ran 5.0 s against 0.35 s. Memory is near a tie: both are dominated by the
`e_m` memo, and bottom-up's is 3–5% larger because it evaluates `e_m` on every
multiset of size `t` rather than only the ones a search reaches.

What top-down still has is no ceiling — it never holds a level. The one cell
found where bottom-up gives up due to its ceiling, though, is one top-down could
not finish either.

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

All 7,175 such cells across the committed $ℤ_n$ tables satisfy that criterion;
`tests/verify_against_literature.py` checks it, along with the rule that everything
outside a computed range carries `?`.

Every table is computed to the full width of its header, so the only `?` in the
committed data are below the diagonal.

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

Ten suites run, in a few seconds:

 - `regression` and `regression_bottom_up` replay 176 known-good values sampled
   from `Experimental tables/` across all nine tables — once with each of the
   [two searches](#the-other-search).
 - `methods_agree` and `methods_agree_f4` run both searches over every cell of a
   small sweep and fail on any disagreement.
 - `literature_verification` checks the committed tables against the theorems
   proved in the accompanying thesis and classical zero-sum results from the
   wider literature.
 - `rings` checks the rings themselves: ring axioms, and that each quotient
   reproduces the built-in ring it generalises.
 - `skip_rules`, `max_work` and `memo_cap` drive those three options through the
   CLI.

See [tests/README.md](tests/README.md).

## Source layout
| File | Contents |
| --- | --- |
| `main.cpp` | CLI and the table writer. |
| `egz_bottom_up.hpp` | The search: `e_m`, the level sweep, `EGZ(t, m)`. |
| `egz_top_down.hpp` | The other search, an independent second implementation. |
| `rings.hpp` | Ring implementations (`Zn`, `Znp`, `F4`, `Z_2_over`, `product`). |
| `quotient.hpp` | Parses and builds the `Z_n[x]/(P)` rings named at run time. |
| `ring_registry.hpp` | Maps a `--ring` name or spec to its ring. |
| `sequence.hpp` | Multiset of ring elements, with hashing for memoization. |
| `config.hpp` | Search bounds (`M_MAX`, `T_MIN`, `T_MAX`), the work budget, and the memo cap. |
| `memo_table.hpp` | The `e_m` memo shared by both searches, with second-chance eviction under `--memo-cap`. |
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
