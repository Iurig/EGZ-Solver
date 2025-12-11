template <typename R>
class sequence {
 private:
  int _size = 0;
 public:
  static constexpr int n = R::order;
  sequence() { c = vector<int>(n); }
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
    _size -= a;
    c[x.value] -= a;
  }
  void insert(const R &x, int a = 1) {
    _size += a;
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
  size_t size() { return _size; }
  bool empty() { return size() == 0; }
  bool operator<(const sequence<R> &other) const { return c < other.c; }
  size_t count(const R &x) { return c[x.value]; }
  vector<int> c;
};