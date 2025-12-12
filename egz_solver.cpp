#ifdef DEBUG
#include <iostream>
#endif

#include "rings.cpp"
#include "sequence.cpp"

using namespace std;

template <typename R>
class EGZSolver {
private:
  unordered_map<sequence<R>, R> memorized_e_m[M_MAX];

public:
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
    if (minimum == R::order)
      return S.size() == t && e_m(S, m) == 0;
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

  // Tries to find a CounterExample of sequence of size "size" that has a
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
    else if (minimum == R::order - 1) {
      bool isCE = !checkSubsets(t, m, prev);
      return isCE;
    } else
      return nonZero;
  }

  // Calculates EGZ(t, m) for ring R
  int EGZ(int t, int m) {
    sequence<R> t_choose_m;
    t_choose_m.insert(R::unit, t);
    if (e_m(t_choose_m, m) != 0) {
#ifdef DEBUG
      cout << "em = " << e_m(t_choose_m, m).value << ", t = " << t << ", m = " << m << endl;
#endif
      return 0;
    }
    int l = t + 1;
    while (CE(t, m, l)) {
      l++;
#ifdef DEBUG
      cout << "testing t = " << t << " and m = " << m << ", l = " << l << endl;
#endif
    }
    return l;
  }
};