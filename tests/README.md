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

176 cases cover all nine tables and run in about two seconds. They were selected
for speed: every case took under 25 ms when generated, except for `Z_7` and
`Z_8`, whose cheapest cells are slower and are included anyway so that no ring
goes uncovered.

## Bounds

The fixture reaches `m = 37`, well past the default `M_MAX` of 20, so
`run_regression` is compiled with `-DEGZ_M_MAX=60 -DEGZ_T_MAX=300`. Cases
outside whatever bounds the binary was built with are reported as `SKIP` rather
than run, so building with the defaults yields 100 passed / 76 skipped instead of
a failure.

## Regenerating the fixture

`table_probe` reads a table, recomputes each cell that holds a value, compares
it against the stored value, and prints the result with a timing:

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

## Skip rules

`test_skip.py` drives the `--skip` expressions through the CLI: which rows reach
the table, that skipped rows are omitted rather than blanked, that the file stays
well formed, and that malformed expressions are rejected. It runs under `ctest`
as `skip_rules`.

```sh
python tests/test_skip.py build/egz-solver
```

## Work budgets

`test_max_work.py` checks the property that makes `--max-work` safe to use: a
cell that completes under a budget equals the cell an unbudgeted run produces,
no cell is invented, and none is silently dropped. It also checks that a larger
budget never solves fewer cells. Runs under `ctest` as `max_work`.

```sh
python tests/test_max_work.py build/egz-solver
```

## Ring structure

`test_rings.py` checks the rings themselves rather than the search. It runs
under `ctest` as `rings`.

```sh
python tests/test_rings.py build/dump_ring
python tests/test_rings.py build/dump_ring --solver build/egz-solver   # adds end to end
```

`dump_ring` prints a ring's order, characteristic, unit and full operation
tables; `ring_goldens.tsv` holds a dump of every registered ring. Six things
are checked:

 - **Axioms.** Every registered ring is a commutative unit ring, verified over
   all `order^3` triples. A wrong multiplication table does not crash the
   solver, it just yields wrong EGZ constants, so nothing else would catch it.
 - **Stability.** Every ring still matches its golden. The published tables were
   computed with these exact operation tables, so a silent change to one
   invalidates data already committed.
 - **The reference quotient.** A short, independent implementation of
   `Z_n[x1..xk]` modulo one relation per variable, written in the test file,
   reproduces the goldens of `Z_2`, `Z_3`, `Z_5`, `Z_2x_by_x2` and `F_4`
   exactly -- in one variable and in several, since a relation of degree 1 kills
   its variable and collapses the ring back onto a smaller one.
 - **The runtime quotient.** Each entry in `EXPECTED` is asked for by `--ring`
   spec and must agree with both the reference and the golden, and must report a
   name usable in `EGZ_<name>.tsv`. A spec the binary does not accept reports
   `PEND` rather than passing silently.
 - **End to end.** With `--solver`, a small EGZ table computed for the spec is
   compared against one computed for the ring it generalises. Equal operation
   tables do not by themselves mean equal output; this covers `e_m`, the
   counterexample search and the memo as well.
 - **Multivariate rings with no counterpart.** `Z_2[x,y]/(x^2,y^2)` and friends
   match nothing on file, so all that can be asked is that they are rings, have
   the expected order, and agree with the reference. These stay small: both
   checks are cubic or worse in the order, in Python.

The reference is the one that pays for the file. With the basis taken in a fixed
order -- `{1, x, ..., x^(d-1)}` in one variable, the box of exponent vectors in
several -- and an element `(a_0..a_(dim-1))` at index `sum(a_j n^j)`, the
quotient does not merely become *isomorphic* to the ring it generalises: it
becomes *identical*, same elements and same indices. So the check is exact table
equality rather than a search for an isomorphism. It was written before the
generic ring existed, which is why the target is a fixed thing to hit rather than
one fitted to whatever came out.

It discriminates: the three order-4 characteristic-2 rings in the registry
(`F_4`, `Z_2x_by_x2`, `Z_2^2`) are told apart from each other, and a wrong
modulus such as `x^2+1` in place of `x^2+x+1` is rejected as not even
isomorphic. When two rings differ but *are* isomorphic, the failure says so,
since that means the basis ordering is wrong rather than the ring.

Regenerate the goldens after deliberately changing a ring:

```sh
./build/dump_ring --all > tests/ring_goldens.tsv
```

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
| Blank iff computed and `char(R)` does not divide `C(t,m)` | structural | 18,389 |
| Thm 3.1 — `Z_3`, `m = 3^k` | exact | 62 |
| Thm 3.4 — `Z_4`, `m = 2^k` | exact | 48 |
| Thm 3.5 — `Z_4`, `m` not a power of 2 | exact | 818 |
| Thm 3.3 — `Z_2^2`, where `2\|t` or `m = 2^r` | exact | 737 |
| Prop 4.1/4.2 — `m = p^k` | conjecture | 139 |
| Thm 3.9 — Caro-Schmitt bound | upper bound | 3,289 |
| Thm 3.8 — ring-only bound | upper bound | 5,188 |
| Thm 2.2 — `Z_2`, every `m` (needs `--solver`) | exact | 242 |

Theorems 3.4 and 3.5 between them account for every one of the 866 value-holding
cells of `EGZ_Z_4.tsv`, so that table is confirmed in full.

Two caveats worth keeping in mind when reading a passing run:

 - Propositions 4.1 and 4.2 are conjectures. Agreement there is mutual
   consistency, not independent confirmation.
 - Theorem 2.2 is the only check that computes rather than reads. It is also
   the only exact result covering arbitrary `m` on a ring, which makes it the
   strongest evidence that the solver is right for the non-prime-power rows
   that no other theorem reaches.
