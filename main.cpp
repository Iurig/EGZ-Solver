#include <stdio.h>

#include <assert.h>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "sequence.cpp"

#define DEBUG false
#define TO_FILE false

unordered_map<sequence<target_ring>, target_ring> memorized_e_m[M_MAX];

// Calculates e_m(S)
template <typename R>
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

// Checks all subsets of a sequence S of size t whose e_m is 0, returns true if
// such subsequence exists, false otherwise
template <typename R>
bool checkSubsets(int t, int m, sequence<R> &S, int minimum = 0) {
  bool subsetZero = false;
  if (minimum == R::order)
    return S.size() == t && e_m(S, m) == 0;
  int removed = 0;
  while (S.size() >= t) {
    subsetZero = subsetZero || checkSubsets<R>(t, m, S, minimum + 1);
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
template <typename R>
bool CE(int t, int m, int size, sequence<R> prev = sequence<R>(),
        int minimum = 0) {
  bool nonZero = false;
  while (prev.size() < size && !nonZero) {
    if (minimum < R::order - 1)
      nonZero = nonZero || CE<R>(t, m, size, prev, minimum + 1);
    prev.insert(minimum);
  }
  if (nonZero == true)
    return nonZero;
  else if (minimum == R::order - 1) {
    bool isCE = !checkSubsets<R>(t, m, prev);
    return isCE;
  } else
    return nonZero;
}

// Calculates EGZ(t, R, m)
template <typename R>
int EGZ(int t, int m) {
  sequence<R> t_choose_m;
  t_choose_m.insert(R::unit, t);
  if (e_m(t_choose_m, m) != 0) {
    cout << "em = " << e_m(t_choose_m, m).value << ", t = " << t
         << ", m = " << m << endl;
    return 0;
  }
  int l = t + 1;
  while (CE<R>(t, m, l)) {
    l++;
    if (DEBUG)
      cout << "testing t = " << t << " and m = " << m << ", l = " << l << endl;
  }
  return l;
}

template <typename R>
void findEGZs(int m_max = M_MAX) {
  ofstream output_file;
  if (TO_FILE)
    output_file.open("EGZ_" + to_string(R::order) + ".txt");
  for (int m = 1; m < m_max; m++) {
    if (TO_FILE)
      output_file << m << "\t";
    if (R::skip(m)) {
      if (TO_FILE)
        output_file << "\n";
      continue;
    }
    if (TO_FILE)
      for (int i = 0; i < m; i++)
        output_file << "\t";
    for (int t = T_MIN(m); t < T_MAX(m); t++) {
      int e = EGZ<R>(t, m);
      if (max(e - t, -1) == -1)
        continue;
      cout << "EGZ(" << t << ", " << R::name() << ", " << m << ") = " << e
           << endl;
      cout << "EGZ-t = " << max(e - t, -1) << endl;
      cout << endl;
      if (TO_FILE)
        output_file << e - t << "\t";
    }
    if (TO_FILE)
      output_file << "\n";
  }
}

int main() {
  // basic testing
  assert(EGZ<target_ring>(16, 8) == 33);
  assert(EGZ<target_ring>(17, 8) == 33);
}
