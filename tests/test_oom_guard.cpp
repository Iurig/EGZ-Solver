// Checks what the bottom-up search does when it runs out of memory.
//
//   test_oom_guard
//
// The e_m memo grows with every cell and is never dropped, so on a large ring
// it can exhaust memory -- and a failed allocation used to abort the process,
// losing every row after the cell as well as the cell. Out of memory is a cell
// the search cannot do, which is what EGZ_ABANDONED already means. This pins
// that: the failure comes back as EGZ_ABANDONED rather than an escaping
// exception, the solver still works afterwards, and it gives the same answers,
// the memo being a cache whose loss costs only time.
//
// The failure is forced rather than provoked, by a global operator new that
// throws while armed: deterministic and instant, where really filling 16 GB
// of RAM would be neither.
#include <cstdlib>
#include <iostream>
#include <new>

#include "egz_bottom_up.hpp"
#include "rings.hpp"

namespace {
// Armed only around the calls being tested; everything else -- iostream, the
// ring tables, startup -- allocates normally.
bool fail_allocations = false;
} // namespace

void *operator new(std::size_t size) {
  if (fail_allocations)
    throw std::bad_alloc();
  if (void *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

static int failures = 0;

static void check(bool ok, const std::string &what, const std::string &detail = "") {
  std::cout << (ok ? "  PASS  " : "  FAIL  ") << what;
  if (!detail.empty())
    std::cout << "  (" << detail << ")";
  std::cout << std::endl;
  if (!ok)
    failures++;
}

int main() {
  using R = Zn<3>;
  const int t = 5, m = 2;

  BottomUpEGZSolver<R> solver;

  const int before = solver.EGZ(t, m);
  check(before != EGZ_ABANDONED, "the cell is answerable to begin with", "EGZ(5, Z_3, 2) = " + std::to_string(before));

  // A second solver: the first one's memo already holds this cell, and could
  // answer without allocating, making the test vacuous.
  BottomUpEGZSolver<R> starved;
  int under_pressure = 0;
  bool escaped = false;
  try {
    fail_allocations = true;
    under_pressure = starved.EGZ(t, m);
    fail_allocations = false;
  } catch (...) {
    // Anything reaching here would be an abort in the real solver, which has
    // no handler above this point.
    fail_allocations = false;
    escaped = true;
  }
  check(!escaped, "an allocation failure does not escape EGZ");
  check(under_pressure == EGZ_ABANDONED, "a starved cell is abandoned, not answered",
        "got " + std::to_string(under_pressure));

  // The point of abandoning rather than aborting: the run goes on. The same
  // solver has to be usable and correct afterwards -- and its memo was dropped
  // on the way out, so this covers a solver emptied mid-life.
  const int after = starved.EGZ(t, m);
  check(after == before, "the same solver answers correctly once memory is back",
        "before " + std::to_string(before) + ", after " + std::to_string(after));

  // A cell it had never seen, so the memo is rebuilt rather than just read.
  const int fresh_before = solver.EGZ(t + 1, m);
  const int fresh_after = starved.EGZ(t + 1, m);
  check(fresh_after == fresh_before, "a cell computed after recovery matches an unstarved solver",
        "unstarved " + std::to_string(fresh_before) + ", recovered " + std::to_string(fresh_after));

  std::cout << (failures ? "\nSOME CHECKS FAILED" : "\nALL CHECKS PASSED") << std::endl;
  return failures ? 1 : 0;
}
