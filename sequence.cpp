#include <cstdlib>
#include <functional>
#include <vector>

#include "config.cpp"

using namespace std;

template <typename R>
class sequence {
private:
  int _size = 0;

public:
  static constexpr int n = R::order;
  sequence() { c = vector<int>(n); }
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
  size_t size() { return _size; }
  long long identifier() const {
    long long h = 0;
    long long t = 1;
    for (int i = 1; i < R::order; i++) {
      h += t * c[i];
      t *= T_MAX();
    }
    return h;
  }
  bool operator==(const sequence<R> &o) const {
    return identifier() == o.identifier();
  }
  bool empty() { return size() == 0; }
  bool operator<(const sequence<R> &other) const { return c < other.c; }
  size_t count(const R &x) { return c[x.value]; }
  vector<int> c;
};

namespace std {
template <typename R>
struct hash<sequence<R>> {
  size_t operator()(const sequence<R> &s) const { return s.identifier(); }
};
} // namespace std