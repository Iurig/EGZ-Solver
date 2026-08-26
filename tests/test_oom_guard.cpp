// Checks what the bottom-up search does when it runs out of memory.
//
//   test_oom_guard
//
// The e_m memo grows with every cell of a table and is never dropped, because
// that reuse is most of why the search is fast. On a large ring it can exhaust
// memory, and an allocation that fails there used to abort the process -- so a
// sweep lost not just the cell it was on but every row after it, and left a
// half-written table behind.
//
// Running out of memory is a cell the search cannot do, which is what
// EGZ_ABANDONED already means. This test pins that:
//
//   1. a failing allocation comes back as EGZ_ABANDONED, not as an exception
//      escaping into the caller,
//   2. the solver still works afterwards, and
//   3. gives the same answer as before, since the memo is a cache and dropping
//      it costs time and nothing else.
//
// Allocation failure is forced rather than provoked: a global operator new that
// throws while armed. That makes the test deterministic and instant, where
// really filling 16 GB of RAM would be neither.
#include <cstdlib>
#include <iostream>
#include <new>

#include "egz_bottom_up.hpp"
#include "rings.hpp"

namespace {
// Armed only around the calls being tested. Everything else in the process --
// iostream, the ring tables, this file's own startup -- allocates normally.
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

  // A second solver, so the first one's memo -- already holding this cell --
  // cannot let it answer without allocating and make the test vacuous.
  BottomUpEGZSolver<R> starved;
  int under_pressure = 0;
  bool escaped = false;
  try {
    fail_allocations = true;
    under_pressure = starved.EGZ(t, m);
    fail_allocations = false;
  } catch (...) {
    // Whatever reaches here would have been an abort in the real solver: there
    // is no handler for it anywhere above this point.
    fail_allocations = false;
    escaped = true;
  }
  check(!escaped, "an allocation failure does not escape EGZ");
  check(under_pressure == EGZ_ABANDONED, "a starved cell is abandoned, not answered",
        "got " + std::to_string(under_pressure));

  // The point of abandoning rather than aborting: the run goes on. The same
  // solver, no longer starved, has to be usable and correct -- its memo was
  // dropped on the way out of the failure, so this also covers a solver that
  // has been emptied mid-life.
  const int after = starved.EGZ(t, m);
  check(after == before, "the same solver answers correctly once memory is back",
        "before " + std::to_string(before) + ", after " + std::to_string(after));

  // A cell it had never seen, on the same recovered solver, so the memo is
  // being rebuilt rather than merely read.
  const int fresh_before = solver.EGZ(t + 1, m);
  const int fresh_after = starved.EGZ(t + 1, m);
  check(fresh_after == fresh_before, "a cell computed after recovery matches an unstarved solver",
        "unstarved " + std::to_string(fresh_before) + ", recovered " + std::to_string(fresh_after));

  std::cout << (failures ? "\nSOME CHECKS FAILED" : "\nALL CHECKS PASSED") << std::endl;
  return failures ? 1 : 0;
}
