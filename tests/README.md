# Tests

`run_regression` replays known-good `EGZ(t, m)` values against the current
solver. The values come from the tables in `Experimental tables/`, so the suite
answers one question: *does the code still produce the numbers this project has
already published?*

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

## The fixture

`regression_cases.tsv` holds one case per line:

```
ring<TAB>m<TAB>t<TAB>expected_EGZ
```

`expected_EGZ` is the absolute constant, **not** the `EGZ - t` value stored in
the tables — the conversion happens when the fixture is generated.

164 cases cover all nine tables and run in about two seconds. They were selected
for speed: every case took under 25 ms when generated, except for `Z_7` and
`Z_8`, whose cheapest cells are slower and are included anyway so that no ring
goes uncovered.

## Bounds

The fixture reaches `m = 37`, well past the default `M_MAX` of 20, so
`run_regression` is compiled with `-DEGZ_M_MAX=60 -DEGZ_T_MAX=300`. Cases
outside whatever bounds the binary was built with are reported as `SKIP` rather
than run, so building with the defaults yields 88 passed / 76 skipped instead of
a failure.

## Regenerating the fixture

`table_probe` reads a table, recomputes each non-blank cell, compares it against
the stored value, and prints the result with a timing:

```sh
cmake --build build --target table_probe
./build/table_probe Z_3 "Experimental tables/EGZ_Z_3.tsv" 150
```

The third argument is a per-cell budget in milliseconds; once a cell exceeds it
the rest of that row is skipped, since cost grows with `t`. Output columns are:

```
MATCH|MISMATCH<TAB>ring<TAB>m<TAB>t<TAB>expected<TAB>actual<TAB>ms
```

Feed the `MATCH` lines back into `regression_cases.tsv` to extend the suite.
Running it over all nine tables checks 5,395 cells, all of which agree with the
solver.

## Verifying against the published theorems

`verify_against_thesis.py` checks the committed tables against the results
proved in the accompanying thesis. Each theorem is restated in the docstring of
the function that applies it, so the script is self-contained: no PDF, no build,
no third-party packages.

```sh
python tests/verify_against_thesis.py
python tests/verify_against_thesis.py --solver build/egz-solver   # adds Thm 2.2
```

It runs under `ctest` as `thesis_verification`, and exits non-zero if any check
fails. What it covers:

| Check | Kind | Cells |
| --- | --- | --- |
| Layout: header labels, uniform row width | structural | 9 tables |
| Blank iff `char(R)` does not divide `C(t,m)` | structural | 12,066 |
| Thm 3.1 — `Z_3`, `m = 3^k` | exact | 62 |
| Thm 3.4 — `Z_4`, `m = 2^k` | exact | 48 |
| Thm 3.5 — `Z_4`, `m` not a power of 2 | exact | 818 |
| Thm 3.3 — `Z_2^2`, where `2\|t` or `m = 2^r` | exact | 737 |
| Prop 4.1/4.2 — `m = p^k` | conjecture | 139 |
| Thm 3.9 — Caro-Schmitt bound | upper bound | 3,289 |
| Thm 3.8 — ring-only bound | upper bound | 5,188 |
| Thm 2.2 — `Z_2`, every `m` (needs `--solver`) | exact | 242 |

Theorems 3.4 and 3.5 between them account for every one of the 866 non-blank
cells of `EGZ_Z_4.tsv`, so that table is confirmed in full.

Two caveats worth keeping in mind when reading a passing run:

 - Propositions 4.1 and 4.2 are conjectures. Agreement there is mutual
   consistency, not independent confirmation.
 - Theorem 2.2 is the only check that computes rather than reads. It is also
   the only exact result covering arbitrary `m` on a ring, which makes it the
   strongest evidence that the solver is right for the non-prime-power rows
   that no other theorem reaches.
