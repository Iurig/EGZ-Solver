# EGZ Solver
A C++ brute-force calculator for arbitrary degree Erdös-Ginzburg-Ziv constants in finite commutative unit rings.
## Supported Rings
 - $ℤ_n$, for any natural $n$.
 - $ℤ_n^m$, for any natural $n$ and $m$.
 - $𝔽_4$.
 - $\frac{ℤ_2\[x\]}{x^2}$
 - Products of any two of the above, via `product<R, P>`.

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
| `--out-dir DIR` | Where to write the table (default `experimental-tables`). |
| `--no-file` | Print progress only; write nothing. |
| `--quiet` | Suppress per-value progress output. |
| `--list-rings` | List supported ring names and exit. |

The output directory is relative to the working directory and is created if
missing, so run from the repository root to land in `experimental-tables/`.

Be aware that cost grows steeply in both `m` and `t`: full tables for the larger
rings take hours, and a single cell deep in a table can take minutes.

## Output format
One `.tsv` per ring, named `EGZ_<ring>.tsv`. **Rows are `m`, columns are `t`, and
a cell holds `EGZ(t, m) - t`, not `EGZ(t, m)` itself.** The first row is the `t`
header; the first column is `m`.

So a cell containing `6` at row `m = 1`, column `t = 21` means `EGZ(21, 1) = 27`.

A cell is blank in any of these cases, which the format does not distinguish:
 - `t < T_MIN(m)`, i.e. `t < m` — left of the diagonal, never computed.
 - `t >= T_MAX(m)` — beyond the requested bound.
 - `EGZ(t, m) - t <= -1`, including `EGZ(t, m) == 0`, which is the solver's way
   of reporting that no EGZ constant exists for that `(t, m)`.

For $ℤ_n$ a cell is non-blank exactly when $\binom{t}{m} \equiv 0 \pmod n$, which
is a useful sanity check on a generated table.

All 6,125 cells across the committed $ℤ_n$ tables satisfy that criterion.

Note that `EGZ_Z_2x_by_x2.tsv` was generated with a per-row bound on `t` --
`smallestPowerBiggerThan(2, m) + m + 1`, the form that survives commented out in
`config.hpp` -- rather than the flat bound the other eight tables use. Its rows
therefore stop well short of the header width, and those trailing blanks mean
"not computed" rather than "no EGZ constant".

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

 - `regression` replays 164 known-good values sampled from `experimental-tables/`
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
3. Optionally override `static bool skip(int m)` to leave rows out of the table.

## License

The code is under the [MIT License](LICENSE); the computed tables in
`experimental-tables/` are under
[CC BY 4.0](experimental-tables/LICENSE). Copyright (c) 2026 Iuri Grangeiro
Carvalho.

The split is deliberate: Creative Commons licenses are not intended for
software, and a software license sits awkwardly on what is essentially
mathematical data. In practice this means you may use the solver freely with
attribution preserved in the source, and reuse or republish the tables provided
you give credit. See [experimental-tables/README.md](experimental-tables/README.md)
for a suggested citation.
