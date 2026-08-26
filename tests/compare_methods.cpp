// Runs both EGZ implementations over a range of cells, checks they agree, and
// times each.
//
//   compare_methods <ring> <m-min> <m-max> <t-max> [per-cell-ms]
//
// The two are independent down to their own e_m and memo, so an agreement is
// evidence and a disagreement localises a bug to one of them. TopDownEGZSolver
// hunts a counterexample at each candidate length; BottomUpEGZSolver sweeps
// every multiset level by level. See the top of egz_bottom_up.hpp.
//
// The optional budget is milliseconds per method: a cell where one side exceeds
// it stops that side for the rest of the row, since cost grows steeply with t.
// Cells only one side finished are reported as such, not as agreements.
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "egz_bottom_up.hpp"
#include "egz_top_down.hpp"
#include "ring_registry.hpp"

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Totals {
  int agree = 0, disagree = 0, oneSided = 0, abandoned = 0;
  double topMs = 0, bottomMs = 0;
};

// A cell either method gave up on says nothing about the other, so it must not
// count as a disagreement -- which is what a bare == would do, EGZ_ABANDONED
// being just another int.
static std::string cellText(int v) {
  return v == EGZ_ABANDONED ? std::string("?") : std::to_string(v);
}

template <typename R>
static int compare(int m_min, int m_max, int t_max, double budget_ms) {
  TopDownEGZSolver<R> top;
  BottomUpEGZSolver<R> bottom;
  Totals tot;

  std::cout << "m\tt\ttop-down\tbottom-up\ttop ms\tbottom ms\tverdict" << std::endl;
  for (int m = m_min; m < m_max; m++) {
    bool topOver = false, bottomOver = false;
    for (int t = T_MIN(m); t < t_max; t++) {
      int a = 0, b = 0;
      double ta = 0, tb = 0;
      if (!topOver) {
        Clock::time_point s = Clock::now();
        a = top.EGZ(t, m);
        ta = msSince(s);
        if (budget_ms > 0 && ta > budget_ms)
          topOver = true;
      }
      if (!bottomOver) {
        Clock::time_point s = Clock::now();
        b = bottom.EGZ(t, m);
        tb = msSince(s);
        if (budget_ms > 0 && tb > budget_ms)
          bottomOver = true;
      }
      const bool ranTop = ta > 0 || !topOver, ranBottom = tb > 0 || !bottomOver;
      if (!ranTop && !ranBottom)
        continue;

      const char *verdict;
      if (!ranTop || !ranBottom) {
        verdict = "ONE-SIDED";
        tot.oneSided++;
      } else if (a == EGZ_ABANDONED || b == EGZ_ABANDONED) {
        // Which side hit its ceiling is the point: this is where the two stop
        // being interchangeable.
        verdict = (a == EGZ_ABANDONED) ? "ABANDONED-top" : "ABANDONED-bottom";
        tot.abandoned++;
      } else if (a == b) {
        verdict = "agree";
        tot.agree++;
      } else {
        verdict = "DISAGREE";
        tot.disagree++;
      }
      tot.topMs += ta;
      tot.bottomMs += tb;
      std::cout << m << "\t" << t << "\t" << (ranTop ? cellText(a) : "-") << "\t" << (ranBottom ? cellText(b) : "-")
                << "\t" << (long)ta << "\t" << (long)tb << "\t" << verdict << std::endl;
    }
  }

  std::cout << "\n"
            << tot.agree << " agree, " << tot.disagree << " disagree, " << tot.oneSided << " one-sided, "
            << tot.abandoned << " abandoned by one method" << std::endl;
  std::cout << "time    top-down " << (long)tot.topMs << " ms, bottom-up " << (long)tot.bottomMs << " ms" << std::endl;

  // Memory in the terms each method spends it. Both accumulate a memo of e_m
  // results; only bottom-up also holds levels, and that is the term with a
  // ceiling. A memo entry is a sequence plus a value in a hash node, so the
  // estimate is coarse and says so. Level memory is exact: two levels live at
  // one bit per multiset.
  const size_t entryBytes = sizeof(int) * R::order + 64;
  const size_t topMemo = top.memoEntries(), bottomMemo = bottom.memoEntries();
  const unsigned long long peak = bottom.peakLevel();
  std::cout << "memo    top-down " << topMemo << " entries (~" << (topMemo * entryBytes >> 10) << " KiB), bottom-up "
            << bottomMemo << " entries (~" << (bottomMemo * entryBytes >> 10) << " KiB)" << std::endl;
  std::cout << "levels  top-down none, bottom-up peak " << peak << " multisets (" << (peak / 4 >> 10) << " KiB for the "
            << "two live levels)" << std::endl;

  // Which half of the bottom-up search the time goes to: level t evaluates e_m
  // on every multiset of that size, and every level above is set arithmetic.
  const double em = bottom.emMs(), climb = bottom.climbMs();
  const double total = em + climb;
  std::cout << "split   bottom-up e_m at level t " << (long)em << " ms over " << bottom.emCells() << " multisets ("
            << (total > 0 ? (long)(100 * em / total) : 0) << "%), climbing " << (long)climb << " ms over "
            << bottom.climbCells() << " multisets (" << (total > 0 ? (long)(100 * climb / total) : 0) << "%)"
            << std::endl;
  if (tot.agree == 0) {
    // Otherwise a budget too tight for anything to finish on both sides would
    // report success without comparing anything.
    std::cerr << "no cell was computed by both methods; nothing was compared" << std::endl;
    return 1;
  }
  return tot.disagree == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc < 5) {
    std::cerr << "usage: compare_methods <ring> <m-min> <m-max> <t-max> [per-cell-ms]" << std::endl;
    return 2;
  }
  std::string ring = argv[1];
  int m_min = std::stoi(argv[2]), m_max = std::stoi(argv[3]), t_max = std::stoi(argv[4]);
  double budget = argc > 5 ? std::stod(argv[5]) : 0;

  // Both solvers size storage from these at construction, so set them first.
  setSearchBounds(m_max, t_max);

  int rc = 2;
  std::string error;
  bool known = dispatchRing(
      ring, [&](auto tag) { rc = compare<typename decltype(tag)::type>(m_min, m_max, t_max, budget); }, &error);
  if (!known) {
    if (!error.empty())
      std::cerr << ring << ": " << error << std::endl;
    else
      std::cerr << "unknown ring: " << ring << std::endl;
    return 2;
  }
  return rc;
}
