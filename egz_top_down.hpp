#pragma once

#include <iostream>

#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "rings.hpp"
#include "sequence.hpp"

// Computes EGZ(t, m) by taking each candidate length l in turn and searching
// for a counterexample: a sequence of length l with no zero-e_m subsequence of
// length t. The first l with none is the answer.
//
// Not the default -- egz_bottom_up.hpp is faster on every ring measured, since
// each length here restarts over ground the previous one covered. Two things
// keep this one: it never holds a level of multisets, so it has no memory
// ceiling; and it is independent, sharing nothing with the bottom-up search but
// `sequence` and the ring. The suite holds the two to identical tables, which
// is the strongest evidence the published numbers are right -- and only means
// something because neither is written in terms of the other.
//
// See "How the search works" in README.md for the comparison.
template <typename R>
class TopDownEGZSolver {
private:
  // One memo table per m; sized from M_MAX() at construction.
  std::vector<std::unordered_map<sequence<R>, R>> memorized_e_m;

  // Work spent on the cell being computed, and whether it ran out. Counted per
  // EGZ(t, m), not per solver, so one abandoned cell does not penalise the
  // next. Whatever was memoised before the abort stays valid.
  long long work = 0;
  bool aborted = false;

  // Charges one unit. True once this cell's budget is spent, at which point
  // every enclosing call unwinds without memoising anything.
  bool spendWork() {
    if (aborted)
      return true;
    if (workBudget() > 0 && ++work > workBudget())
      aborted = true;
    return aborted;
  }

public:
  TopDownEGZSolver() : memorized_e_m(M_MAX() + 1) {}

  // Work spent on the most recent EGZ(t, m).
  long long lastWork() const { return work; }

  // Memoized e_m results across every degree -- essentially all of this
  // solver's memory, at a sequence plus a value per hash node. Reported by
  // tests/compare_methods.cpp, since the two searches spend memory very
  // differently.
  size_t memoEntries() const {
    size_t total = 0;
    for (const auto &table : memorized_e_m)
      total += table.size();
    return total;
  }

  sequence<R> subseq = sequence<R>();
  // Calculates e_m(S)
  R e_m(sequence<R> &S, int m) {
    if (m == 0)
      return R::unit;
    if (m > S.size())
      return 0;
    if (S.size() == 1)
      return S.element();
    typename std::unordered_map<sequence<R>, R>::const_iterator it = memorized_e_m[m].find(S);
    if (it != memorized_e_m[m].end())
      return it->second;
    // Cache hits are free; only work that recurses is charged.
    if (spendWork())
      return 0;

    R x = S.element();

    S.remove(x);
    R value = x * e_m(S, m - 1) + e_m(S, m);
    S.insert(x);
    // If the budget ran out below us, `value` came from children that returned
    // 0 without finishing, so it must not be cached. Defensive, not a fix for
    // an observed failure: under the current row-major traversal a poisoned
    // entry is only re-read by cells already being abandoned, and dropping this
    // check leaves the output byte-identical over every ring and budget tried.
    // Another visiting order would end that.
    if (aborted)
      return 0;
    memorized_e_m[m][S] = value;
    return value;
  }

  // True if S has a subsequence of size t whose e_m is 0.
  bool checkSubsets(int t, int m, sequence<R> &S, int minimum = 0) {
    bool subsetZero = false;
    if (subseq.size() == t && e_m(subseq, m) == 0 && subseq.is_Subsequence_of(S)) {
      return true;
    }
    if (minimum == R::order) {
      bool found = (S.size() == t && e_m(S, m) == 0);
      if (found)
        subseq = S;
      return found;
    }
    if (spendWork())
      return false;
    int removed = 0;
    while (S.size() >= t) {
      subsetZero = subsetZero || checkSubsets(t, m, S, minimum + 1);
      // Leave the loop but not the function: S still has to be restored below.
      if (aborted)
        break;
      if (S.count(minimum) == 0)
        break;
      else {
        removed++;
        S.remove(minimum);
      }
    }
    S.insert(minimum, removed);
    return subsetZero;
  }

  // True if some sequence of length `size` has no subsequence of size t whose
  // e_m is 0 -- a counterexample.
  bool CE(int t, int m, int size, sequence<R> prev = sequence<R>(), int minimum = 0) {
    if (spendWork())
      return false;
    bool nonZero = false;
    while (prev.size() < size && !nonZero) {
      if (minimum < R::order - 1)
        nonZero = nonZero || CE(t, m, size, prev, minimum + 1);
      if (aborted)
        return false; // prev is by value, so there is nothing to restore
      prev.insert(minimum);
    }
    if (nonZero == true)
      return nonZero;
    else {
      bool isCE = !checkSubsets(t, m, prev);
      if (isCE) {
#ifdef DEBUG
        if (egz::verbose) {
          std::cout << "Found CE of size " << size << " for t = " << t << " and m = " << m << ": ";
          for (int i = 0; i < R::order; i++)
            std::cout << prev.count(i) << " ";
          std::cout << std::endl;
        }
#endif
      }
      return isCE;
    }
  }

  // EGZ(t, m) for ring R. 0 when no EGZ constant exists for this (t, m),
  // EGZ_ABANDONED when --max-work ran out first.
  int EGZ(int t, int m) {
    work = 0;
    aborted = false;
    // m indexes memorized_e_m directly, so an out-of-range m would corrupt
    // memory rather than fail. T_MAX needs no such guard: it is only the
    // identifier() hash radix, and sequence::operator== compares contents.
    if (m < 0 || m >= (int)memorized_e_m.size()) {
      std::cerr << "EGZ: m=" << m << " is outside the compiled bound M_MAX=" << M_MAX() << "; raise --m-max" << std::endl;
      exit(2);
    }
    sequence<R> t_choose_m;
    t_choose_m.insert(R::unit, t);
    R em = e_m(t_choose_m, m);
    if (aborted)
      return EGZ_ABANDONED;
    if (em != 0) {
#ifdef DEBUG
      if (egz::verbose)
        std::cout << "em = " << em.value << ", t = " << t << ", m = " << m << std::endl;
#endif
      return 0;
    }
    int l = t + 1;
    while (CE(t, m, l)) {
      if (aborted)
        return EGZ_ABANDONED;
      l++;
#ifdef DEBUG
      if (egz::verbose)
        std::cout << "testing t = " << t << " and m = " << m << ", l = " << l << std::endl;
#endif
    }
    if (aborted)
      return EGZ_ABANDONED;
    return l;
  }
};