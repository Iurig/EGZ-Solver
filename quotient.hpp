#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "rings.hpp"

// Quotient rings defined at run time: Z_n[x1..xk] modulo one monic relation per
// variable, each relation in that variable alone.
//
//   Z_2[x]/(x^2+x+1)        Z_4[x]/(x^2+1)        Z_2[x,y]/(x^2,y^2)
//
// Every other ring here is a type, fixed when the binary is compiled. This one
// is chosen from a string, so its order, characteristic and operation tables
// are ordinary statics rather than `constexpr`. Nothing downstream minds:
// sequence and EGZSolver read R::order as a value, never in a constant
// expression.
//
// One ring is configured at a time, by configureQuotient(). That matches how
// the program is used -- one table per run -- and keeps an element a bare int,
// which matters because elements are copied constantly in the inner loop.
//
// == Structure ==
//
// The ring is described by a *basis of monomials* and the expansion of each
// pairwise product of basis monomials back into that basis. Multiplication of
// two arbitrary elements then follows by bilinearity and never has to know what
// a monomial is; buildTables() below is written entirely against that.
//
// With one monic relation f_i(x_i) of degree d_i per variable, the basis is the
// box of exponent vectors 0 <= e_i < d_i, and the expansion factorises: each
// x_i^(g_i) reduces on its own, and the product of monomials is the product of
// those univariate expansions. That is what buildBasis() does.
//
// What this deliberately does not cover is a relation mixing variables, such as
// (xy - 1) or (x^2 - y). Those need a Grobner basis over Z_n -- and over
// composite n, the ring version rather than the textbook field one.
struct QuotientDef {
  int n = 0;   // coefficients live in Z_n
  int dim = 0; // number of basis monomials: the product of the degrees
  int order = 0;
  std::string name; // canonical and file-safe
  std::vector<char> vars;
  std::vector<std::vector<int>> polys;        // one monic relation per variable
  std::vector<int> degs;                      // its degree, so dim = product(degs)
  std::vector<std::vector<int>> basisProduct; // dim*dim entries, each dim long
  std::vector<int> addTable, mulTable;        // order*order, flattened
};

// The largest ring this will build. The tables are order^2 entries, and
// sequence allocates a vector of `order` ints per sequence, so the search is
// hopeless long before this -- the cap is here to turn a typo like
// Z_2[x]/(x^40) into a message instead of an allocation failure.
inline constexpr int QUOTIENT_MAX_ORDER = 256;

inline int modn(long long v, int n) { return (int)(((v % n) + n) % n); }

// --- parsing -----------------------------------------------------------------

// Parses one relation: "x2+x+1", "x^2+x+1", "2y^3-1", "x". Coefficients are
// reduced mod n. Both "x^k" and "xk" are accepted for the exponent; the second
// is the form a ring's name uses, so a name parses back to the same ring.
//
// `var` is set to the variable seen. A relation in two variables is rejected
// here rather than silently mishandled: the basis below assumes each relation
// constrains one variable.
inline bool parseRelation(const std::string &s, int n, std::vector<int> &out, char &var, std::string &err) {
  out.assign(1, 0);
  var = 0;
  if (s.empty()) {
    err = "empty relation";
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
      // A term like "xy" lands here, having already consumed the x.
      if (std::isalpha((unsigned char)s[i]) && var && s[i] != var)
        err = std::string("relation '") + s + "' mixes " + var + " and " + s[i] +
              "; each relation must be in one variable";
      else
        err = "expected + or - before '" + s.substr(i) + "'";
      return false;
    }
    if (i >= s.size()) {
      err = "trailing sign in '" + s + "'";
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
    if (i < s.size() && std::isalpha((unsigned char)s[i])) {
      char seen = s[i++];
      if (var && seen != var) {
        err = std::string("relation '") + s + "' mixes " + var + " and " + seen +
              "; each relation must be in one variable";
        return false;
      }
      var = seen;
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
        err = "expected an exponent after '^' in '" + s + "'";
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

// A relation as it appears in a ring's name: x2+x+1, x2, x, 2y2+y+1.
inline std::string renderPoly(const std::vector<int> &poly, char var) {
  std::string s;
  for (int i = (int)poly.size() - 1; i >= 0; i--) {
    if (poly[i] == 0)
      continue;
    if (!s.empty())
      s += "+";
    if (poly[i] != 1 || i == 0)
      s += std::to_string(poly[i]);
    if (i >= 1)
      s += var;
    if (i >= 2)
      s += std::to_string(i);
  }
  return s.empty() ? "0" : s;
}

// The exponent vector of basis monomial `idx`, variable 0 varying fastest. For
// one variable this is the identity, which is what keeps a univariate quotient
// byte-identical to the hand-written ring it generalises.
inline void monomialExponents(const QuotientDef &d, int idx, std::vector<int> &e) {
  e.resize(d.degs.size());
  for (size_t i = 0; i < d.degs.size(); i++) {
    e[i] = idx % d.degs[i];
    idx /= d.degs[i];
  }
}

inline int monomialIndex(const QuotientDef &d, const std::vector<int> &e) {
  int idx = 0;
  for (int i = (int)d.degs.size() - 1; i >= 0; i--)
    idx = idx * d.degs[i] + e[i];
  return idx;
}

// The basis, and the expansion of every product of two basis monomials into it.
//
// Reduction rests on each relation being monic: x^d = -(f_0 + ... + f_(d-1)
// x^(d-1)) is a rewrite rule only when the leading coefficient is a unit.
// parseSpec has already divided through by it.
inline void buildBasis(QuotientDef &d) {
  const int n = d.n, k = (int)d.degs.size();

  // powers[i][g] is x_i^g written in {1, x_i, ..., x_i^(d_i - 1)}, for every g
  // a product of two basis monomials can reach.
  std::vector<std::vector<std::vector<int>>> powers(k);
  for (int i = 0; i < k; i++) {
    const int di = d.degs[i];
    powers[i].assign(2 * di - 1, std::vector<int>(di, 0));
    for (int g = 0; g < di; g++)
      powers[i][g][g] = 1;
    for (int g = di; g < 2 * di - 1; g++) {
      // Multiply the previous power by x_i, then fold the overflowing term.
      int carry = powers[i][g - 1][di - 1];
      for (int c = di - 1; c >= 1; c--)
        powers[i][g][c] = powers[i][g - 1][c - 1];
      powers[i][g][0] = 0;
      for (int c = 0; c < di; c++)
        powers[i][g][c] = modn((long long)powers[i][g][c] - (long long)carry * d.polys[i][c], n);
    }
  }

  // The product of monomials e and f is prod_i x_i^(e_i + f_i), and each factor
  // expands on its own, so the coefficient of basis monomial c in the product is
  // the product over i of the univariate coefficients.
  d.basisProduct.assign((size_t)d.dim * d.dim, std::vector<int>(d.dim, 0));
  std::vector<int> e(k), f(k), c(k);
  for (int a = 0; a < d.dim; a++) {
    monomialExponents(d, a, e);
    for (int b = 0; b < d.dim; b++) {
      monomialExponents(d, b, f);
      std::vector<int> &out = d.basisProduct[(size_t)a * d.dim + b];
      for (int m = 0; m < d.dim; m++) {
        monomialExponents(d, m, c);
        long long coef = 1;
        for (int i = 0; i < k && coef; i++)
          coef = coef * powers[i][e[i] + f[i]][c[i]] % n;
        out[m] = (int)coef;
      }
    }
  }
}

// Full element tables, from the basis expansion by bilinearity.
inline void buildTables(QuotientDef &d) {
  const int dim = d.dim, n = d.n, order = d.order;
  std::vector<std::vector<int>> digits(order, std::vector<int>(dim, 0));
  for (int v = 0; v < order; v++) {
    int k = v;
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
          for (int m = 0; m < dim; m++)
            acc[m] = modn((long long)acc[m] + (long long)c * e[m], n);
        }
      }
      d.mulTable[(size_t)a * order + b] = index(acc);
    }
  }
}

// Splits "a,b,c" on commas, or "a_and_b_and_c" on the separator a name uses.
inline std::vector<std::string> splitRelations(const std::string &s, const std::string &sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = s.find(sep, start);
    if (p == std::string::npos) {
      out.push_back(s.substr(start));
      return out;
    }
    out.push_back(s.substr(start, p - start));
    start = p + sep.size();
  }
}

// Accepts either spelling:
//   Z_<n>[<vars>]/(<relations>)   the mathematical form; needs quoting in a shell
//   Z_<n><vars>_by_<relations>    what R::name() returns, so a name parses back
//
// with relations separated by "," in the first and "_and_" in the second:
//   Z_2[x,y]/(x^2,y^2)            Z_2xy_by_x2_and_y2
inline bool parseSpec(const std::string &spec, QuotientDef &d, std::string &err) {
  const std::string shapes = "expected Z_n[vars]/(relations) or Z_nvars_by_relations, "
                             "for example Z_2[x]/(x^2+x+1) or Z_2[x,y]/(x^2,y^2)";
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

  std::string rest = spec.substr(i), varList, body, sep;
  size_t close = rest.find("]/");
  if (!rest.empty() && rest[0] == '[' && close != std::string::npos) {
    varList = rest.substr(1, close - 1);
    body = rest.substr(close + 2);
    if (!body.empty() && body.front() == '(') {
      if (body.back() != ')') {
        err = "unbalanced parentheses in " + spec;
        return false;
      }
      body = body.substr(1, body.size() - 2);
    }
    sep = ",";
    // Drop the commas between variable names: [x,y] and [xy] mean the same.
    std::string letters;
    for (char c : varList)
      if (c != ',' && c != ' ')
        letters += c;
    varList = letters;
  } else {
    size_t by = rest.find("_by_");
    if (by == std::string::npos) {
      err = shapes;
      return false;
    }
    varList = rest.substr(0, by);
    body = rest.substr(by + 4);
    sep = "_and_";
  }

  std::vector<char> vars;
  for (char c : varList) {
    if (!std::isalpha((unsigned char)c)) {
      err = std::string("'") + c + "' is not a variable name";
      return false;
    }
    if (std::find(vars.begin(), vars.end(), c) != vars.end()) {
      err = std::string("variable ") + c + " is named twice";
      return false;
    }
    vars.push_back(c);
  }
  if (vars.empty()) {
    err = "no variables; " + shapes;
    return false;
  }

  // One relation per variable, each in that variable alone. Anything else is a
  // ring this cannot represent, not a ring it gets wrong.
  std::vector<std::string> parts = splitRelations(body, sep);
  if (parts.size() != vars.size()) {
    err = std::to_string(vars.size()) + " variable(s) but " + std::to_string(parts.size()) +
          " relation(s); each variable needs exactly one";
    return false;
  }
  std::vector<std::vector<int>> polys(vars.size());
  std::vector<bool> seen(vars.size(), false);
  for (const std::string &part : parts) {
    std::vector<int> poly;
    char var = 0;
    if (!parseRelation(part, n, poly, var, err))
      return false;
    if (!var) {
      err = "relation '" + part + "' has no variable, so it fixes nothing";
      return false;
    }
    auto it = std::find(vars.begin(), vars.end(), var);
    if (it == vars.end()) {
      err = std::string("relation '") + part + "' is in " + var + ", which is not one of the variables";
      return false;
    }
    size_t slot = it - vars.begin();
    if (seen[slot]) {
      err = std::string("two relations in ") + var + "; each variable needs exactly one";
      return false;
    }
    seen[slot] = true;
    polys[slot] = poly;
  }

  long long dim = 1, order = 1;
  std::vector<int> degs(vars.size());
  for (size_t v = 0; v < vars.size(); v++) {
    std::vector<int> &poly = polys[v];
    int deg = (int)poly.size() - 1;
    if (deg < 1) {
      err = std::string("the relation in ") + vars[v] + " must have degree at least 1";
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
              ", so the relation has no monic multiple and the quotient is not free over Z_" + std::to_string(n);
        return false;
      }
      for (int k = 0; k <= deg; k++)
        poly[k] = modn((long long)poly[k] * inv, n);
    }
    degs[v] = deg;
    dim *= deg;
    if (dim > 32) { // n^dim would pass the cap for every n >= 2
      err = "the basis would have " + std::to_string(dim) + " monomials, far past anything searchable";
      return false;
    }
  }
  for (int k = 0; k < dim; k++) {
    order *= n;
    if (order > QUOTIENT_MAX_ORDER) {
      err = "n^(basis size) = " + std::to_string(n) + "^" + std::to_string(dim) + " exceeds the cap of " +
            std::to_string(QUOTIENT_MAX_ORDER) + " elements";
      return false;
    }
  }

  d.n = n;
  d.vars = vars;
  d.polys = polys;
  d.degs = degs;
  d.dim = (int)dim;
  d.order = (int)order;
  d.name = "Z_" + std::to_string(n);
  for (char c : vars)
    d.name += c;
  d.name += "_by_";
  for (size_t v = 0; v < vars.size(); v++)
    d.name += (v ? "_and_" : "") + renderPoly(polys[v], vars[v]);
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
  // Every relation is monic of degree >= 1, so the ideal contains no nonzero
  // constant and k*1 = 0 exactly when n divides k: the characteristic is n.
  Quotient::unit = 1;
  return true;
}

// True if `spec` is shaped like a quotient at all, so a parse failure can be
// reported as a broken spec rather than an unknown ring name.
inline bool looksLikeQuotient(const std::string &spec) {
  return spec.compare(0, 2, "Z_") == 0 && (spec.find("]/") != std::string::npos || spec.find("_by_") != std::string::npos);
}
