#pragma once

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "egz_solver.hpp"
#include "sequence.hpp"

// A second way to compute EGZ(t, m), working up from the bottom instead of
// searching downward for a counterexample.
//
// EGZSolver asks, for each candidate length l in turn, "is there a sequence of
// length l with no zero-e_m subsequence of length t?", and enumerates sequences
// looking for one. This asks the complementary question level by level:
//
//   level t      which multisets of size t have e_m = 0
//   level t+1    which multisets of size t+1 contain one of those
//   level t+2    which contain one of those
//   ...
//
// and stops at the first level where every multiset is covered. That level is
// EGZ(t, m).
//
// The step is exact and cheap. For |S| > t,
//
//   S is covered  <=>  S - x is covered for some x in S
//
// (<=) a good subsequence of S - x is one of S. (=>) if T is a good subsequence
// of S then |S| > |T|, so some x has more copies in S than in T, and T survives
// in S - x. So a level is one pass over its multisets, each looking at its own
// predecessors -- no search, no backtracking.
//
// Two things follow from the recurrence, both worth knowing before reading a
// benchmark against EGZSolver:
//
//  * Only *which* e_m are zero matters, never their values. The values are
//    computed once at level t and never consulted again.
//  * Every multiset of every level is visited, whereas EGZSolver can stop the
//    moment it finds one counterexample. The trade is exhaustive-but-flat
//    against searching-but-deep, and which wins is an empirical question --
//    hence tests/compare_methods.cpp.
//
// This is deliberately a separate implementation, down to its own e_m and its
// own memo, so that agreement between the two is evidence rather than a
// tautology. Only `sequence` and the ring are shared.
template <typename R>
class BottomUpEGZSolver {
private:
  std::vector<std::unordered_map<sequence<R>, R>> memorized_e_m;

  long long work = 0;
  bool aborted = false;
  unsigned long long peak_level = 0;

  // Saturating Pascal's triangle. Saturating rather than wrapping matters: a
  // wrapped binomial would produce a plausible-looking rank pointing at the
  // wrong multiset, where a saturated one trips the size guard instead.
  static constexpr unsigned long long BINOM_MAX = ~0ULL / 2;
  std::vector<std::vector<unsigned long long>> binom;

  bool spendWork() {
    if (aborted)
      return true;
    if (workBudget() > 0 && ++work > workBudget())
      aborted = true;
    return aborted;
  }

  void buildBinom(int maxA) {
    binom.assign(maxA + 1, std::vector<unsigned long long>(maxA + 2, 0));
    for (int a = 0; a <= maxA; a++) {
      binom[a][0] = 1;
      for (int b = 1; b <= a; b++) {
        unsigned long long l = binom[a - 1][b - 1], r = binom[a - 1][b];
        binom[a][b] = (l > BINOM_MAX - r) ? BINOM_MAX : l + r;
      }
    }
  }

  // Number of multisets of size s over R::order elements.
  unsigned long long levelSize(int s) const { return binom[s + R::order - 1][R::order - 1]; }

  // Position of composition c among the compositions of l, in the same
  // ascending lexicographic order nextComposition walks. Enumerating in that
  // order means a multiset's own rank is just the loop counter; this is only
  // needed for its predecessors, which live on the level below.
  unsigned long long rankOf(const std::vector<int> &c, int l) const {
    const int k = R::order;
    unsigned long long r = 0;
    int rem = l;
    for (int i = 0; i + 1 < k; i++) {
      // Compositions of `rem` whose part i is below c[i], summed by the hockey
      // stick identity rather than a loop.
      const int p = k - i - 1;
      r += binom[rem + p][p] - binom[rem - c[i] + p][p];
      rem -= c[i];
    }
    return r;
  }

  // Next composition of the same total in ascending lexicographic order.
  static bool nextComposition(std::vector<int> &c, int k) {
    long long suffix = c[k - 1];
    for (int i = k - 2; i >= 0; i--) {
      if (suffix > 0) {
        c[i]++;
        for (int j = i + 1; j <= k - 2; j++)
          c[j] = 0;
        c[k - 1] = (int)(suffix - 1);
        return true;
      }
      suffix += c[i];
    }
    return false;
  }

  static void firstComposition(std::vector<int> &c, int k, int l) {
    c.assign(k, 0);
    c[k - 1] = l;
  }

  R e_m(sequence<R> &S, int m) {
    if (m == 0)
      return R::unit;
    if (m > (int)S.size())
      return 0;
    if (S.size() == 1)
      return S.element();
    auto it = memorized_e_m[m].find(S);
    if (it != memorized_e_m[m].end())
      return it->second;
    if (spendWork())
      return 0;

    R x = S.element();
    S.remove(x);
    R value = x * e_m(S, m - 1) + e_m(S, m);
    S.insert(x);
    if (aborted)
      return 0;
    memorized_e_m[m][S] = value;
    return value;
  }

public:
  // Largest level this will hold in memory, as a multiset count. Two levels are
  // live at once, one bit each, so this is a handful of megabytes; the point is
  // to abandon a hopeless (t, m) rather than to die allocating.
  static constexpr unsigned long long MAX_LEVEL_SIZE = 40000000ULL;

  BottomUpEGZSolver() : memorized_e_m(M_MAX() + 1) {}

  long long lastWork() const { return work; }

  // Memoized e_m results held. Unlike the top-down search this is not where the
  // memory goes: only level t consults e_m at all, so the memo stops growing
  // once the sweep starts climbing.
  size_t memoEntries() const {
    size_t total = 0;
    for (const auto &table : memorized_e_m)
      total += table.size();
    return total;
  }

  // Multisets in the largest level allocated, over the solver's lifetime. Two
  // levels are live at once at one bit each, so peak level memory is about
  // peakLevel() / 4 bytes. This is the cost the top-down search does not pay,
  // and the reason this one has a ceiling.
  unsigned long long peakLevel() const { return peak_level; }

  // Same contract as EGZSolver::EGZ: 0 when no EGZ constant exists, and
  // EGZ_ABANDONED when --max-work or the level cap stopped it early.
  //
  // A work unit here is one multiset visited, which is not the unit EGZSolver
  // charges, so the same --max-work means different things to the two.
  int EGZ(int t, int m) {
    work = 0;
    aborted = false;
    if (m < 0 || m >= (int)memorized_e_m.size()) {
      std::cerr << "EGZ: m=" << m << " is outside the compiled bound M_MAX=" << M_MAX() << "; raise --m-max" << std::endl;
      exit(2);
    }
    const int k = R::order;
    if (t < 1)
      return 0;

    // Same first question EGZSolver asks. If e_m of t copies of 1 is not zero
    // then 1, 1, 1, ... is a counterexample of every length, so no constant
    // exists -- and the level loop below would never terminate.
    sequence<R> t_ones;
    t_ones.insert(R::unit, t);
    R em = e_m(t_ones, m);
    if (aborted)
      return EGZ_ABANDONED;
    if (em != 0)
      return 0;

    // Levels are indexed by rank, so the binomials have to reach the largest
    // level considered. The bound on l is the same one that makes the search
    // finite: past it, every multiset has a repeated element in such quantity
    // that the answer cannot still be open.
    const int maxLevel = T_MAX() + M_MAX() + k + 2;
    buildBinom(maxLevel + k + 2);

    if (levelSize(t) > MAX_LEVEL_SIZE)
      return EGZ_ABANDONED;

    // Level t: covered means e_m is zero. This is the only place values are
    // looked at; everything above is pure set arithmetic.
    peak_level = std::max(peak_level, levelSize(t));
    std::vector<bool> cur((size_t)levelSize(t), false);
    std::vector<int> c;
    firstComposition(c, k, t);
    size_t idx = 0;
    do {
      if (spendWork())
        return EGZ_ABANDONED;
      sequence<R> S;
      for (int i = 0; i < k; i++)
        if (c[i])
          S.insert(R(i), c[i]);
      if (e_m(S, m) == 0)
        cur[idx] = true;
      idx++;
    } while (nextComposition(c, k));
    if (aborted)
      return EGZ_ABANDONED;

    for (int l = t + 1; l <= maxLevel; l++) {
      if (levelSize(l) > MAX_LEVEL_SIZE)
        return EGZ_ABANDONED;
      peak_level = std::max(peak_level, levelSize(l));
      std::vector<bool> next((size_t)levelSize(l), false);
      unsigned long long coveredCount = 0;
      firstComposition(c, k, l);
      idx = 0;
      do {
        if (spendWork())
          return EGZ_ABANDONED;
        for (int x = 0; x < k; x++) {
          if (!c[x])
            continue;
          c[x]--;
          bool below = cur[(size_t)rankOf(c, l - 1)];
          c[x]++;
          if (below) {
            next[idx] = true;
            coveredCount++;
            break;
          }
        }
        idx++;
      } while (nextComposition(c, k));

#ifdef DEBUG
      if (egz::verbose)
        std::cout << "level l = " << l << ": " << coveredCount << " of " << idx << " covered, t = " << t
                  << ", m = " << m << std::endl;
#endif
      if (coveredCount == idx)
        return l;
      cur = std::move(next);
    }
    return EGZ_ABANDONED;
  }
};
