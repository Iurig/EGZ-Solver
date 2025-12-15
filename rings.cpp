#include <string>

using namespace std;

// Check this class for prerequisites of children classes
class ring {
public:
  int value;

  // Don't forget declaring a static constexpr int characteristic and order,
  // as well as name

  // Constructor
  ring(int value = 0) : value(value) {}

  // Virtual destructor to support polymorphic deletion
  virtual ~ring() = default;

  // Condition for skipping EGZ calculations (useful if certain m values are
  // slow)
  static bool skip(int m) { return false; };

  // Operator +
  ring operator+(const ring &other);

  // Operator *
  ring operator*(const ring &other);

  // Operator ==
  bool operator==(const ring &other) { return value == other.value; };

  // Operator !=
  bool operator!=(const ring &other) { return value != other.value; };

  // Operator <
  bool operator<(const ring &other) { return value < other.value; };

  // Ring name
  string name() { return "ring"; }
};

template <int n>
class Zn : public ring {
public:
  static constexpr int characteristic = n, order = n, unit = 1;

  // Constructor
  Zn(int value = 0) : ring(value % n) {}

  static bool skip(int m) {
    while (m % order == 0) {
      m /= order;
    }
    return false; // printing everybody, delete if you want to skip prime powers
    return m == 1;
  }

  // Operator +
  Zn operator+(const Zn &other) const { return Zn(value + other.value); }

  // Operator -
  Zn operator-(const Zn &other) const { return Zn(value - other.value); }

  // Operator *
  Zn operator*(const Zn &other) const { return Zn(value * other.value); }

  // Operator <
  bool operator<(const Zn &other) const { return (value < other.value); }

  // Ring name
  static string name() { return "Z_" + to_string(n); }
};

template <int n, int p>
class Znp : public ring {
public:
  static constexpr int characteristic = p, order = p * n;
  static constexpr int unitCalc() {
    int power = 1, resp = 0;
    for (int i = 0; i < p; i++) {
      resp += power;
      power *= n;
    }
    return resp;
  }
  static constexpr int unit = unitCalc();

  Zn<n> expression[p];
  Znp(int index = 0) {
    value = index;
    for (int i = p - 1; i >= 0; i--) {
      expression[i] = Zn<n>(index);
      index = index / n;
    }
  }

  static bool skip(int m) { return false; };

  // Operator +
  Znp operator+(const Znp &other) {
    Znp sum;
    for (int i = 0; i < p; i++) {
      sum.expression[i] = expression[i] + other.expression[i];
      sum.value *= n;
      sum.value += sum.expression[i].value;
    }
    return sum;
  }

  // Operator *
  Znp operator*(const Znp &other) {
    Znp prod;
    for (int i = 0; i < p; i++) {
      prod.expression[i] = expression[i] * other.expression[i];
      prod.value *= n;
      prod.value += prod.expression[i].value;
    }
    return prod;
  }

  // Ring name
  static string name() { return "Z_" + to_string(n) + "^" + to_string(p); };
};

class Z_2_over : public ring {
public:
  static constexpr int characteristic = 4, order = 4, unit = 1;
  // clang-format off
  static constexpr int sum[4][4] = // +      0   1   x   x+1
                                   //------------------------
      {{0, 1, 2, 3},               // 0      0   1   x   x+1
       {1, 0, 3, 2},               // 1      1   0   x+1 x
       {2, 3, 0, 1},               // x      x   x+1 0   1
       {3, 2, 1, 0}},              // x+1    x+1 x   1   0
      prod[4][4] =                 // *      0   1   x   x+1
                                   //------------------------
      {{0, 0, 0, 0},               // 0      0   0   0   0
       {0, 1, 2, 3},               // 1      0   1   x   x+1
       {0, 2, 0, 2},               // x      0   x   0   x
       {0, 3, 2, 1}};              // x+1    0   x+1 x   1
  // clang-format on

  // Constructor
  Z_2_over(int value = 0) : ring(value) {}

  // Operator +
  Z_2_over operator+(const Z_2_over &other) const { return Z_2_over(sum[value][other.value]); }

  // Operator *
  Z_2_over operator*(const Z_2_over &other) const { return Z_2_over(prod[value][other.value]); }

  // Ring name
  static string name() { return "Z_2x_by_x2"; }
};

class F4 : public ring {
public:
  static constexpr int characteristic = 4, order = 4, unit = 1;
  // clang-format off
  static constexpr int sum[4][4] = // +      0   1   x   x+1
                                   //------------------------
      {{0, 1, 2, 3},               // 0      0   1   x   x+1
       {1, 0, 3, 2},               // 1      1   0   x+1 x
       {2, 3, 0, 1},               // x      x   x+1 0   1
       {3, 2, 1, 0}},              // x+1    x+1 x   1   0
      prod[4][4] =                 // *      0   1   x   x+1
                                   //------------------------
      {{0, 0, 0, 0},               // 0      0   0   0   0
       {0, 1, 2, 3},               // 1      0   1   x   x+1
       {0, 2, 3, 1},               // x      0   x   x+1 1
       {0, 3, 1, 2}};              // x+1    0   x+1 1   x
  // clang-format on

  // Constructor
  F4(int value = 0) : ring(value) {}

  // Operator +
  F4 operator+(const F4 &other) const { return F4(sum[value][other.value]); }

  // Operator *
  F4 operator*(const F4 &other) const { return F4(prod[value][other.value]); }

  // Ring name
  static string name() { return "F_4"; }
};

template <typename R, typename P>
class product : public ring {
public:
  static constexpr int characteristic = lcm(R::characteristic, P::characteristic), order = R::order * P::order,
                       unit = R::unit * P::order + P::unit;

  // Constructor
  product(int r = 0, int p = 0) : ring(r * P::order + p) {}

  // Operator +
  product operator+(const product &other) const {
    return product((R(value / P::order) + R(other.value / P::order)).value, (P(value % P::order) + P(other.value % P::order)).value);
  }

  // Operator *
  product operator*(const product &other) const {
    return product((R(value / P::order) * R(other.value / P::order)).value, (P(value % P::order) * P(other.value % P::order)).value);
  }

  // Ring name
  static string name() { return "(" + R::name() + ", " + R::name() + ")"; };
};
