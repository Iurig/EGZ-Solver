#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

#include "config.hpp"


template <typename R>
class sequence {
private:
  int _size = 0;

public:
  static constexpr int n = R::order;
  sequence() { c = std::vector<int>(n); }
  R element() {
    for (int i = n - 1; i >= 0; i--)
      if (c[i] != 0)
        return R(i);
    return R();
  }
  void remove(const R &x, int a = 1) {
    _size -= a;
    c[x.value] -= a;
  }
  void insert(const R &x, int a = 1) {
    _size += a;
    c[x.value] += a;
  }
  std::size_t size() { return _size; }
  long long identifier() const {
    long long h = 0;
    long long t = 1;
    for (int i = 1; i < R::order; i++) {
      h += t * c[i];
      t *= T_MAX();
    }
    return h;
  }
  // Exact counterpart to identifier(), which hashes multiplicities of the
  // non-zero elements only. Ignoring c[0] is deliberate and sound for the
  // memoized quantity: adding a zero element contributes 0 * e_{m-1}, so
  // e_m is unchanged by it, and conflating those sequences keeps the memo
  // much smaller. Comparing the counts rather than the identifiers means a
  // hash collision costs a bucket probe instead of returning a wrong entry.
  bool operator==(const sequence<R> &o) const {
    for (int i = 1; i < n; i++)
      if (c[i] != o.c[i])
        return false;
    return true;
  }
  bool empty() { return size() == 0; }
  bool operator<(const sequence<R> &other) const { return c < other.c; }
  std::size_t count(const R &x) { return c[x.value]; }
  std::vector<int> c;
  void print() {
    for (int i = 0; i < n; i++) {
      std::cout << c[i] << " ";
    }
    std::cout << std::endl;
  }
  bool is_Subsequence_of(const sequence<R> &S) {
    for (int i = 0; i < n; i++) {
      if (c[i] > S.c[i])
        return false;
    }
    return true;
  }
};

namespace std {
template <typename R>
struct hash<sequence<R>> {
  size_t operator()(const sequence<R> &s) const { return s.identifier(); }
};
} // namespace std