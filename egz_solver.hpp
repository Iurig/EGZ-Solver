#pragma once

#include <iostream>

#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "rings.hpp"
#include "sequence.hpp"


template <typename R>
class EGZSolver {
private:
  // One memo table per m; sized from M_MAX() at construction.
  std::vector<std::unordered_map<sequence<R>, R>> memorized_e_m;

public:
  EGZSolver() : memorized_e_m(M_MAX() + 1) {}

  sequence<R> subseq = sequence<R>();
  // Calculates e_m(S)
  R e_m(sequence<R> &S, int m) {
    if (m == 0)
      return R::unit;
    if (m > S.size())
      return 0;
    if (S.size() == 1)
      return S.element();
    if (memorized_e_m[m].find(S) != memorized_e_m[m].end())
      return memorized_e_m[m][S];

    R x = S.element();

    R &resp = memorized_e_m[m][S];
    S.remove(x);
    resp = x * e_m(S, m - 1) + e_m(S, m);
    S.insert(x);
    return resp;
  }

  // Checks all subsets of a sequence S of size t whose e_m is 0, returns true
  // if such subsequence exists, false otherwise
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
    int removed = 0;
    while (S.size() >= t) {
      subsetZero = subsetZero || checkSubsets(t, m, S, minimum + 1);
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

  // Tries to find a CounterExample of sequence of size "size" that has no
  // subsequence of size t whose e_m is 0, returns true if it finds it, false
  // otherwise
  bool CE(int t, int m, int size, sequence<R> prev = sequence<R>(), int minimum = 0) {
    bool nonZero = false;
    while (prev.size() < size && !nonZero) {
      if (minimum < R::order - 1)
        nonZero = nonZero || CE(t, m, size, prev, minimum + 1);
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

  // Calculates EGZ(t, m) for ring R.
  // Returns 0 when no EGZ constant exists for this (t, m).
  int EGZ(int t, int m) {
    // m indexes memorized_e_m directly, so an out-of-range m would corrupt
    // memory rather than fail. T_MAX needs no such guard: it is only the
    // identifier() hash radix, and sequence::operator== compares contents.
    if (m < 0 || m >= (int)memorized_e_m.size()) {
      std::cerr << "EGZ: m=" << m << " is outside the compiled bound M_MAX=" << M_MAX() << "; raise --m-max" << std::endl;
      exit(2);
    }
    sequence<R> t_choose_m;
    t_choose_m.insert(R::unit, t);
    if (e_m(t_choose_m, m) != 0) {
#ifdef DEBUG
      if (egz::verbose)
        std::cout << "em = " << e_m(t_choose_m, m).value << ", t = " << t << ", m = " << m << std::endl;
#endif
      return 0;
    }
    int l = t + 1;
    while (CE(t, m, l)) {
      l++;
#ifdef DEBUG
      if (egz::verbose)
        std::cout << "testing t = " << t << " and m = " << m << ", l = " << l << std::endl;
#endif
    }
    return l;
  }
};