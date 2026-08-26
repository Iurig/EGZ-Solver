#pragma once

#include <iostream>
#include <numeric>
#include <string>

// Check this class for prerequisites of children classes
class ring {
public:
  int value;

  // Don't forget declaring a static constexpr int characteristic, order and
  // unit, as well as name

  // Constructor
  ring(int value = 0) : value(value) {}

  // Virtual destructor to support polymorphic deletion
  virtual ~ring() = default;

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
  std::string name() { return "ring"; }
};

template <int n>
class Zn : public ring {
public:
  static constexpr int characteristic = n, order = n, unit = 1;

  // Constructor. The double reduction is not redundant: C++ keeps the sign of
  // the dividend, so a bare value % n leaves operator- below returning a
  // negative value, and sequence indexes its storage by it.
  Zn(int value = 0) : ring(((value % n) + n) % n) {}

  // Operator +
  Zn operator+(const Zn &other) const { return Zn(value + other.value); }

  // Operator -
  Zn operator-(const Zn &other) const { return Zn(value - other.value); }

  // Operator *
  Zn operator*(const Zn &other) const { return Zn(value * other.value); }

  // Operator <
  bool operator<(const Zn &other) const { return (value < other.value); }

  // Ring name
  static std::string name() { return "Z_" + std::to_string(n); }
};

template <int n, int p>
class Znp : public ring {
public:
  // Znp<n, p> is (Z_n)^p: p components, each in Z_n. So it has n^p elements and
  // characteristic n -- not p * n and p, which only coincide when n = p = 2.
  static constexpr int orderCalc() {
    int e = 1;
    for (int i = 0; i < p; i++)
      e *= n;
    return e;
  }
  static constexpr int characteristic = n, order = orderCalc();
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
    for (int i = 0; i < p; i++) {
      index *= n;
    }
    value -= index;
  }

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
  Znp print() {
    for (int i = 0; i < p; i++) {
      std::cout << expression[i].value << " ";
    }
    std::cout << std::endl;
    return 0;
  }

  // Ring name
  static std::string name() { return "Z_" + std::to_string(n) + "^" + std::to_string(p); };
};

class Z_2_over : public ring {
public:
  // 4 elements but characteristic 2: every element satisfies a + a = 0.
  static constexpr int characteristic = 2, order = 4, unit = 1;
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
  static std::string name() { return "Z_2x_by_x2"; }
};

class F4 : public ring {
public:
  // 4 elements but characteristic 2: every element satisfies a + a = 0.
  static constexpr int characteristic = 2, order = 4, unit = 1;
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
  static std::string name() { return "F_4"; }
};

template <typename R, typename P>
class product : public ring {
public:
  static constexpr int characteristic = std::lcm(R::characteristic, P::characteristic), order = R::order * P::order,
                       unit = R::unit * P::order + P::unit;

  // Constructor. Takes an element index in [0, order), like every other ring
  // here: sequence and TopDownEGZSolver identify elements that way and use the value
  // to index storage, so a constructor taking a component pair would put
  // elements out of range. Components are index / P::order and index % P::order.
  product(int index = 0) : ring(index) {}

  static product fromParts(int r, int p) { return product(r * P::order + p); }

  R first() const { return R(value / P::order); }
  P second() const { return P(value % P::order); }

  // Operator +
  product operator+(const product &other) const {
    return fromParts((first() + other.first()).value, (second() + other.second()).value);
  }

  // Operator *
  product operator*(const product &other) const {
    return fromParts((first() * other.first()).value, (second() * other.second()).value);
  }

  // Ring name. Kept free of spaces and brackets: it becomes part of the output
  // file name, EGZ_<name>.tsv.
  static std::string name() { return R::name() + "x" + P::name(); };
};
