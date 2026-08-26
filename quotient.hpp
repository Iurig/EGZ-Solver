#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "rings.hpp"

// Quotient rings defined at run time: Z_n[x]/(P).
//
// Every other ring here is a type, fixed when the binary is compiled. This one
// is chosen from a string, so its order, characteristic and operation tables
// are ordinary statics rather than `constexpr`. Nothing downstream minds:
// sequence and EGZSolver read R::order as a value, never in a constant
// expression. See "Runtime rings" in README.md.
//
// One ring is configured at a time, by configureQuotient(). That matches how
// the program is used -- one table per run -- and keeps an element a bare int,
// which matters because elements are copied constantly in the inner loop.
//
// == Structure, and the step to several variables ==
//
// The ring is described by a *basis of monomials* and the expansion of each
// pairwise product of basis monomials back into that basis. For Z_n[x]/(P) with
// P monic of degree d, the basis is {1, x, ..., x^(d-1)} and the expansion of
// x^i * x^j is x^(i+j) reduced by P. Multiplication of two arbitrary elements
// then follows by bilinearity, and never has to know what a monomial is.
//
// That is the whole reason the code is shaped this way. Z_n[x1..xk] modulo one
// monic relation per variable has a basis of exponent vectors and the same
// bilinear expansion; only buildBasis() below would change.
struct QuotientDef {
  int n = 0;                                    // coefficients live in Z_n
  int dim = 0;                                  // number of basis monomials
  int order = 0;                                // n^dim
  std::string name;                             // canonical and file-safe
  std::vector<int> poly;                        // monic P, lowest degree first
  std::vector<std::vector<int>> basisProduct;   // dim*dim entries, each dim long
  std::vector<int> addTable, mulTable;          // order*order, flattened
};

// The largest ring this will build. The tables are order^2 entries, and
// sequence allocates a vector of `order` ints per sequence, so the search is
// hopeless long before this -- the cap is here to turn a typo like
// Z_2[x]/(x^40) into a message instead of an allocation failure.
inline constexpr int QUOTIENT_MAX_ORDER = 256;

inline int modn(long long v, int n) { return (int)(((v % n) + n) % n); }

// --- parsing -----------------------------------------------------------------

// Parses "x2+x+1", "x^2+x+1", "2x^3-1", "1". Coefficients are reduced mod n.
// Both "x^k" and "xk" are accepted for the exponent: the second is the form
// R::name() emits, so a ring's name parses back to the same ring.
inline bool parsePoly(const std::string &s, int n, std::vector<int> &out, std::string &err) {
  out.assign(1, 0);
  if (s.empty()) {
    err = "empty polynomial";
    return false;
  }
  size_t i = 0;
  while (i < s.size()) {
    int sign = 1;
    if (s[i] == '+') {
      i++;
    } else if (s[i] == '-') {
      sign = -1;
      i++;
    } else if (i != 0) {
      err = "expected + or - before '" + s.substr(i) + "'";
      return false;
    }
    if (i >= s.size()) {
      err = "trailing sign";
      return false;
    }
    size_t start = i;
    long long coef = 1;
    bool hasCoef = false;
    while (i < s.size() && std::isdigit((unsigned char)s[i]))
      i++;
    if (i > start) {
      coef = std::stoll(s.substr(start, i - start));
      hasCoef = true;
    }
    int exponent = 0;
    if (i < s.size() && (s[i] == 'x' || s[i] == 'X')) {
      i++;
      exponent = 1;
      bool caret = (i < s.size() && s[i] == '^');
      if (caret)
        i++;
      size_t es = i;
      while (i < s.size() && std::isdigit((unsigned char)s[i]))
        i++;
      if (i > es)
        exponent = std::stoi(s.substr(es, i - es));
      else if (caret) {
        err = "expected an exponent after '^'";
        return false;
      }
    } else if (!hasCoef) {
      err = "expected a term at '" + s.substr(start) + "'";
      return false;
    }
    if (exponent > 64) {
      err = "degree " + std::to_string(exponent) + " is far past anything searchable";
      return false;
    }
    if ((int)out.size() <= exponent)
      out.resize(exponent + 1, 0);
    out[exponent] = modn((long long)out[exponent] + sign * coef, n);
  }
  while (out.size() > 1 && out.back() == 0)
    out.pop_back();
  return true;
}

// P as it appears in a ring's name: x2+x+1, x2, x, 2x2+x+1.
inline std::string renderPoly(const std::vector<int> &poly) {
  std::string s;
  for (int i = (int)poly.size() - 1; i >= 0; i--) {
    if (poly[i] == 0)
      continue;
    if (!s.empty())
      s += "+";
    if (poly[i] != 1 || i == 0)
      s += std::to_string(poly[i]);
    if (i >= 1)
      s += "x";
    if (i >= 2)
      s += std::to_string(i);
  }
  return s.empty() ? "0" : s;
}

// Basis {1, x, ..., x^(d-1)} and the expansion of every x^i * x^j into it.
//
// Reduction rests on P being monic: x^d = -(P_0 + ... + P_(d-1) x^(d-1)), which
// is only a rewrite rule if the leading coefficient is a unit. parseSpec has
// already divided through by it.
inline void buildBasis(QuotientDef &d) {
  const int dim = d.dim, n = d.n;
  // powers[k] is x^k written in the basis, for k up to 2*(dim-1).
  std::vector<std::vector<int>> powers(2 * dim - 1, std::vector<int>(dim, 0));
  for (int k = 0; k < dim; k++)
    powers[k][k] = 1;
  for (int k = dim; k < 2 * dim - 1; k++) {
    // Multiply the previous power by x, then fold the overflowing x^dim term.
    int carry = powers[k - 1][dim - 1];
    for (int i = dim - 1; i >= 1; i--)
      powers[k][i] = powers[k - 1][i - 1];
    powers[k][0] = 0;
    for (int i = 0; i < dim; i++)
      powers[k][i] = modn((long long)powers[k][i] - (long long)carry * d.poly[i], n);
  }
  d.basisProduct.assign((size_t)dim * dim, std::vector<int>(dim, 0));
  for (int i = 0; i < dim; i++)
    for (int j = 0; j < dim; j++)
      d.basisProduct[(size_t)i * dim + j] = powers[i + j];
}

// Full element tables, from the basis expansion by bilinearity.
inline void buildTables(QuotientDef &d) {
  const int dim = d.dim, n = d.n, order = d.order;
  std::vector<std::vector<int>> digits(order, std::vector<int>(dim, 0));
  for (int v = 0, k; v < order; v++) {
    k = v;
    for (int i = 0; i < dim; i++) {
      digits[v][i] = k % n;
      k /= n;
    }
  }
  auto index = [&](const std::vector<int> &a) {
    int k = 0;
    for (int i = dim - 1; i >= 0; i--)
      k = k * n + modn(a[i], n);
    return k;
  };

  d.addTable.assign((size_t)order * order, 0);
  d.mulTable.assign((size_t)order * order, 0);
  std::vector<int> acc(dim);
  for (int a = 0; a < order; a++) {
    for (int b = 0; b < order; b++) {
      for (int i = 0; i < dim; i++)
        acc[i] = digits[a][i] + digits[b][i];
      d.addTable[(size_t)a * order + b] = index(acc);

      std::fill(acc.begin(), acc.end(), 0);
      for (int i = 0; i < dim; i++) {
        if (!digits[a][i])
          continue;
        for (int j = 0; j < dim; j++) {
          if (!digits[b][j])
            continue;
          const std::vector<int> &e = d.basisProduct[(size_t)i * dim + j];
          int c = digits[a][i] * digits[b][j];
          for (int k = 0; k < dim; k++)
            acc[k] = modn((long long)acc[k] + (long long)c * e[k], n);
        }
      }
      d.mulTable[(size_t)a * order + b] = index(acc);
    }
  }
}

// Accepts either spelling:
//   Z_<n>[x]/(<poly>)   the mathematical form; needs quoting in a shell
//   Z_<n>x_by_<poly>    what R::name() returns, so a name parses back
inline bool parseSpec(const std::string &spec, QuotientDef &d, std::string &err) {
  const std::string shapes = "expected Z_n[x]/(P) or Z_nx_by_P, for example Z_2[x]/(x^2+x+1)";
  if (spec.compare(0, 2, "Z_") != 0) {
    err = shapes;
    return false;
  }
  size_t i = 2, ns = i;
  while (i < spec.size() && std::isdigit((unsigned char)spec[i]))
    i++;
  if (i == ns) {
    err = "missing the modulus n; " + shapes;
    return false;
  }
  int n = std::stoi(spec.substr(ns, i - ns));
  if (n < 2) {
    err = "n must be at least 2 (Z_1 is the zero ring)";
    return false;
  }
  std::string rest = spec.substr(i), body;
  if (rest.compare(0, 4, "[x]/") == 0) {
    body = rest.substr(4);
    if (!body.empty() && body.front() == '(') {
      if (body.back() != ')') {
        err = "unbalanced parentheses in " + spec;
        return false;
      }
      body = body.substr(1, body.size() - 2);
    }
  } else if (rest.compare(0, 5, "x_by_") == 0) {
    body = rest.substr(5);
  } else {
    err = shapes;
    return false;
  }

  std::vector<int> poly;
  if (!parsePoly(body, n, poly, err))
    return false;
  int deg = (int)poly.size() - 1;
  if (deg < 1 || poly[deg] == 0) {
    err = "P must have degree at least 1; degree 0 leaves the zero ring";
    return false;
  }
  // A leading coefficient that is a unit mod n can be divided out, which is
  // what makes reduction a rewrite rule. A zero divisor cannot: x^deg would
  // have no unique normal form, and the quotient is not free over Z_n.
  if (poly[deg] != 1) {
    int inv = 0;
    for (int k = 1; k < n; k++)
      if (modn((long long)poly[deg] * k, n) == 1) {
        inv = k;
        break;
      }
    if (!inv) {
      err = "leading coefficient " + std::to_string(poly[deg]) + " is not invertible mod " + std::to_string(n) +
            ", so P has no monic multiple and the quotient is not free over Z_" + std::to_string(n);
      return false;
    }
    for (int k = 0; k <= deg; k++)
      poly[k] = modn((long long)poly[k] * inv, n);
  }

  long long order = 1;
  for (int k = 0; k < deg; k++) {
    order *= n;
    if (order > QUOTIENT_MAX_ORDER) {
      err = "n^deg(P) = " + std::to_string(n) + "^" + std::to_string(deg) + " exceeds the cap of " +
            std::to_string(QUOTIENT_MAX_ORDER) + " elements";
      return false;
    }
  }

  d.n = n;
  d.dim = deg;
  d.order = (int)order;
  d.poly = poly;
  d.name = "Z_" + std::to_string(n) + "x_by_" + renderPoly(poly);
  buildBasis(d);
  buildTables(d);
  return true;
}

// --- the ring ----------------------------------------------------------------

// The ring configureQuotient() last selected. Elements index into its tables.
inline QuotientDef quotientDef;

class Quotient : public ring {
public:
  // Deliberately not constexpr: this ring is not known until run time. Every
  // use of R::order, R::characteristic and R::unit in the solver is a plain
  // value read, so nothing needs them earlier.
  static inline int order = 0, characteristic = 0, unit = 1;

  Quotient(int value = 0) : ring(value) {}

  Quotient operator+(const Quotient &other) const {
    return Quotient(quotientDef.addTable[(size_t)value * order + other.value]);
  }

  Quotient operator*(const Quotient &other) const {
    return Quotient(quotientDef.mulTable[(size_t)value * order + other.value]);
  }

  static std::string name() { return quotientDef.name; }
};

// Selects the ring `spec` names. Returns false and sets `error` if the spec is
// not a well formed quotient.
inline bool configureQuotient(const std::string &spec, std::string &error) {
  QuotientDef d;
  if (!parseSpec(spec, d, error))
    return false;
  quotientDef = std::move(d);
  Quotient::order = quotientDef.order;
  Quotient::characteristic = quotientDef.n;
  // The ideal generated by a monic P of degree >= 1 contains no nonzero
  // constant -- any nonzero multiple of P has degree >= deg(P) -- so k*1 = 0
  // exactly when n divides k, and the characteristic really is n.
  Quotient::unit = 1;
  return true;
}

// True if `spec` is shaped like a quotient at all, so a parse failure can be
// reported as a broken spec rather than an unknown ring name.
inline bool looksLikeQuotient(const std::string &spec) {
  return spec.compare(0, 2, "Z_") == 0 && (spec.find("[x]/") != std::string::npos || spec.find("x_by_") != std::string::npos);
}
