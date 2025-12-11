#include <stdio.h>

#include <fstream>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rings.cpp"

#define DEBUG false
#define TO_FILE false

// Change this to the ring being studied
#define target_ring Znp<2, 2>

using namespace std;

long long smallestPowerBiggerThan(long long base, long long value) {
  long long i = 1;
  while (i <= value) {
    i *= base;
  }
  return i;
}

// Testing for all values of m < M_MAX
const int M_MAX = 30;
int T_MIN(int m = 1) { return m; }
int T_MAX(int m = M_MAX) { return 8 * smallestPowerBiggerThan(2, m); }

template <typename R>
class multiset {
 public:
  int sz = 0;
  static constexpr int n = R::order;
  multiset() { c = vector<int>(n); }
  R first() {
    for (int i = 0; i < n; i++)
      if (c[i] != 0) return R(i);
    return R();
  }
  R last() {
    for (int i = n - 1; i >= 0; i--)
      if (c[i] != 0) return R(i);
    return R();
  }
  void erase(const R &x, int a = 1) {
    sz -= a;
    c[x.value] -= a;
  }
  void insert(const R &x, int a = 1) {
    sz += a;
    c[x.value] += a;
  }
  size_t hash() const {
    long long h = 0;
    long long t = 1;
    for (int i = 1; i < R::order; i++) {
      h += t * c[i];
      t *= T_MAX();
    }
    return h;
  }
  size_t size() { return sz; }
  bool empty() { return size() == 0; }
  bool operator<(const multiset<R> &other) const { return (c < other.c); }
  size_t count(const R &x) { return c[x.value]; }
  void print() {
    for (int i = 0; i < n; i++) {
      cout << c[i] << " ";
    }
    cout << "\n";
  }
  vector<int> c;
};

unordered_map<int, target_ring> memorized_e_m[M_MAX];

// Calculates e_m(S)
template <typename R>
R e_m(multiset<R> &S, int m) {
  if (m == 0) return R::unit;
  if (m > S.size()) return 0;
  if (S.size() == 1) return S.first();
  if (memorized_e_m[m].find(S.hash()) != memorized_e_m[m].end())
    return memorized_e_m[m][S.hash()];
  return calc_e_m(S, m);
}

// Auxiliary function to e_m
template <typename R>
R calc_e_m(multiset<R> &S, int m) {
  R x = S.last();

  S.erase(x);
  R blah = x * e_m(S, m - 1) + e_m(S, m);
  S.insert(x);

  return memorized_e_m[m][S.hash()] = blah;
}

// Checks all subsets of a sequence S of size t whose e_m is 0, returns true if
// such subsequence exists, false otherwise
template <typename R>
bool checkSubsets(int t, int m, multiset<R> &S, int minimum = 0) {
  bool subsetZero = false;
  if (minimum == R::order) return S.size() == t && e_m(S, m) == 0;
  int removed = 0;
  while (S.size() >= t) {
    subsetZero = subsetZero || checkSubsets<R>(t, m, S, minimum + 1);
    if (S.count(minimum) == 0)
      break;
    else {
      removed++;
      S.erase(minimum);
    }
  }
  S.insert(minimum, removed);
  return subsetZero;
}

// Tries to find a CounterExample of sequence of size "size" that has a
// subsequence of size t whose e_m is 0, returns true if it finds it, false
// otherwise
template <typename R>
bool CE(int t, int m, int size, multiset<R> prev = multiset<R>(),
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
  multiset<R> t_choose_m;
  t_choose_m.insert(R::unit, t);
  if (e_m(t_choose_m, m) != 0) {
    printf("em=%d, t=%d, m=%d\n", e_m(t_choose_m, m).value, t, m);
    return 0;
  }
  int l = t + 1;
  while (CE<R>(t, m, l)) {
    l++;
    if (DEBUG) printf("testing t = %d and m = %d, l = %d\n", t, m, l);
  }
  return l;
}

template <typename R>
void findEGZs(int m_max = M_MAX) {
  ofstream myfile;
  if (TO_FILE) myfile.open("EGZ_" + to_string(R::order) + ".txt");
  for (int m = 1; m < m_max; m++) {
    if (TO_FILE) myfile << m << "\t";
    if (TO_FILE && R::skip(m)) {
      myfile << "\n";
      continue;
    }
    if (R::skip(m)) continue;
    if (TO_FILE)
      for (int i = 0; i < m; i++) myfile << "\t";
    for (int t = T_MIN(m); t < T_MAX(m); t++) {
      int e = EGZ<R>(t, m);
      if (max(e - t, -1) == -1) continue;
      printf("EGZ(%d, ", t);
      R::printName();
      printf(", %d) = %d\nEGZ-t = %d\n", m, e, max(e - t, -1));
      printf("\n");
      if (TO_FILE) myfile << e - t << "\t";
    }
    if (TO_FILE) myfile << "\n";
  }
}

int main() {
  findEGZs<target_ring>(M_MAX);
}
