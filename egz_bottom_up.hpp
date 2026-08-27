#pragma once

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

#include "config.hpp"
#include "memo_table.hpp"
#include "sequence.hpp"

#ifndef EGZ_MAX_LEVEL
#define EGZ_MAX_LEVEL 1600000000ULL
#endif

// Computes EGZ(t, m) by working up from level t. The default search.
//
//   level t      which multisets of size t have e_m = 0
//   level t+1    which multisets of size t+1 contain one of those
//   ...
//
// It stops at the first level where every multiset is covered, and that level is EGZ(t, m). The step is exact and cheap because for |S| >
// t,
//
// Only *which* e_m vanish ever matters, never their values, and every multiset of every level is visited, where the top-down search can
// stop at the first counterexample. Visiting everything still wins in memory, because nothing is ever re-derived, but by a small margin.
//
// The implementation shares nothing with the top-down search but `sequence` and the ring.

template <typename R>
class BottomUpEGZSolver {
private:
  // One table per m, sized from M_MAX() at construction. See memo_table.hpp.
  MemoTable<R> memorized_e_m;

  long long work = 0;
  bool aborted = false;
  unsigned long long peak_level = 0;

  // Split of the work between the two halves, over the solver's lifetime: is the cost in finding which e_m vanish, or in climbing the
  // levels above? The code does not say, and the answer decides where optimisation goes.
  double em_ms = 0, climb_ms = 0;
  unsigned long long em_cells = 0, climb_cells = 0;

  // Saturating Pascal's triangle. A wrapped binomial would give a plausible rank pointing at the wrong multiset; a saturated one trips the
  // size guard.
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

  // Position of c among the compositions of l, in the ascending lexicographic order nextComposition walks. That order makes a multiset's
  // own rank the loop counter, so this is only needed for its predecessors one level down.
  unsigned long long rankOf(const std::vector<int> &c, int l) const {
    const int k = R::order;
    unsigned long long r = 0;
    int rem = l;
    for (int i = 0; i + 1 < k; i++) {
      // Compositions of `rem` with part i below c[i], by the hockey stick identity rather than a loop.
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
    if (const R *hit = memorized_e_m.find(m, S))
      return *hit;
    if (spendWork())
      return 0;

    R x = S.element();
    S.remove(x);
    R value = x * e_m(S, m - 1) + e_m(S, m);
    S.insert(x);
    if (aborted)
      return 0;
    memorized_e_m.insert(m, S, value);
    return value;
  }

public:
  // Largest level this will hold, as a multiset count; two are live at once at one bit each, so the default is about 200 MB. Override with
  // -DEGZ_MAX_LEVEL= to trade memory for reach.
  static constexpr unsigned long long MAX_LEVEL_SIZE = EGZ_MAX_LEVEL;

  BottomUpEGZSolver() : memorized_e_m(M_MAX() + 1) {}

  long long lastWork() const { return work; }

  // Memoized e_m results held. Unlike the top-down search, not where the memory goes: only level t consults e_m, so this stops growing once
  // the sweep starts climbing.
  size_t memoEntries() const { return memorized_e_m.size(); }

  // Multisets in the largest level allocated, over the solver's lifetime; two are live at once at one bit each, so about peakLevel() / 4
  // bytes. The cost the top-down search does not pay, and why this one has a ceiling.
  unsigned long long peakLevel() const { return peak_level; }

  double emMs() const { return em_ms; }
  double climbMs() const { return climb_ms; }
  unsigned long long emCells() const { return em_cells; }
  unsigned long long climbCells() const { return climb_cells; }

  // Same contract as the top-down search: 0 when no EGZ constant exists, EGZ_ABANDONED when --max-work, the level cap, or memory stopped it
  // early. A work unit here is one multiset visited, not what the top-down search charges, so --max-work means different things to the two.
  //
  // MAX_LEVEL_SIZE bounds a level and --memo-cap the memo, which otherwise grows with every cell and is never dropped because that reuse
  // is most of the speed. Out of memory is a cell this search cannot do, which is what EGZ_ABANDONED means. The memo goes with it: holding
  // a cache no later cell can afford would abandon those too.
  int EGZ(int t, int m) {
    try {
      return search(t, m);
    } catch (const std::bad_alloc &) {
      dropMemo();
      return EGZ_ABANDONED;
    } catch (const std::length_error &) {
      // A level too large for vector<bool> to index. Only reachable past MAX_LEVEL_SIZE, but the same answer.
      dropMemo();
      return EGZ_ABANDONED;
    }
  }

private:
  // Frees the memo, keeping the per-m indexing. Runs from the catch handler above, so it must not allocate: a fresh table to swap in would
  // ask for the memory it is releasing, and a throw out of a catch handler is the abort being avoided. clear() only releases, and the nodes
  // are where memory is.
  void dropMemo() { memorized_e_m.clear(); }

  int search(int t, int m) {
    work = 0;
    aborted = false;
    if (m < 0 || m >= memorized_e_m.levels()) {
      std::cerr << "EGZ: m=" << m << " is outside the compiled bound M_MAX=" << M_MAX() << "; raise --m-max" << std::endl;
      exit(2);
    }
    const int k = R::order;
    if (t < 1)
      return 0;

    // The same first question the top-down search asks. If e_m of t copies of 1 is not zero, then 1, 1, 1, ... is a counterexample of every
    // length: no constant exists, and the level loop below would never terminate.
    sequence<R> t_ones;
    t_ones.insert(R::unit, t);
    R em = e_m(t_ones, m);
    if (aborted)
      return EGZ_ABANDONED;
    if (em != 0)
      return 0;

    // Levels are indexed by rank, so the binomials must reach the largest level considered. That bound is what makes the search finite:
    // past it, every multiset repeats an element too often for the answer to still be open.
    const int maxLevel = T_MAX() + M_MAX() + k + 2;
    buildBinom(maxLevel + k + 2);

    if (levelSize(t) > MAX_LEVEL_SIZE)
      return EGZ_ABANDONED;

    // Level t: covered means e_m is zero. The only place values are looked at; everything above is pure set arithmetic.
    peak_level = std::max(peak_level, levelSize(t));
    std::chrono::steady_clock::time_point em_start = std::chrono::steady_clock::now();
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
    em_cells += idx;
    em_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - em_start).count();
    if (aborted)
      return EGZ_ABANDONED;

    for (int l = t + 1; l <= maxLevel; l++) {
      if (levelSize(l) > MAX_LEVEL_SIZE)
        return EGZ_ABANDONED;
      std::chrono::steady_clock::time_point climb_start = std::chrono::steady_clock::now();
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
      climb_cells += idx;
      climb_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - climb_start).count();

#ifdef DEBUG
      if (egz::verbose)
        std::cout << "level l = " << l << ": " << std::fixed << std::setprecision(2) << std::setw(5)
                  << 100.0 * (float(coveredCount) / float(idx)) << "% covered, t = " << t << ", m = " << m << std::endl;
#endif
      if (coveredCount == idx)
        return l;
      cur = std::move(next);
    }
    return EGZ_ABANDONED;
  }
};
