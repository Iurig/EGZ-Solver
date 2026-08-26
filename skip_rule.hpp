#pragma once

#include <string>
#include <utility>
#include <vector>

// A predicate on m, built from --skip expressions. Skipped rows are left out of
// the table rather than written blank, so an absent row means "not computed"
// and cannot be read as "no EGZ constant exists".
//
// Recognised expressions (see "Skipping rows" in README.md):
//
//   pow:K        skip m that are powers of K (K^0 = 1 counts)
//   powers       pow:<order of the ring being computed>
//   mod:K=R      keep only m congruent to R modulo K; skip the rest
//   list:a,b,c   skip exactly these m
//   none         skip nothing
//
// A row is skipped if any expression says so, so two mod: rules keep only the
// m satisfying both.
class SkipRule {
public:
  // Parses one expression, using ring_order for the bare "powers" form. False,
  // with `error` set, if it is malformed.
  bool add(const std::string &spec, int ring_order, std::string &error) {
    if (spec == "none")
      return true;
    if (spec == "powers")
      return addPower(ring_order, error);
    if (starts(spec, "pow:")) {
      int k = 0;
      if (!parseInt(spec.substr(4), k, error))
        return false;
      return addPower(k, error);
    }
    if (starts(spec, "mod:")) {
      std::string rest = spec.substr(4);
      size_t eq = rest.find('=');
      if (eq == std::string::npos) {
        error = "expected mod:K=R";
        return false;
      }
      int k = 0, r = 0;
      if (!parseInt(rest.substr(0, eq), k, error) || !parseInt(rest.substr(eq + 1), r, error))
        return false;
      if (k < 1) {
        error = "modulus must be at least 1";
        return false;
      }
      if (r < 0 || r >= k) {
        error = "residue must satisfy 0 <= R < K";
        return false;
      }
      keep_mod_.push_back({k, r});
      return true;
    }
    if (starts(spec, "list:")) {
      std::string rest = spec.substr(5), item;
      for (size_t i = 0; i <= rest.size(); i++) {
        if (i == rest.size() || rest[i] == ',') {
          int v = 0;
          if (!parseInt(item, v, error))
            return false;
          explicit_.push_back(v);
          item.clear();
        } else {
          item.push_back(rest[i]);
        }
      }
      return true;
    }
    error = "unrecognised skip expression";
    return false;
  }

  bool skips(int m) const {
    for (int k : powers_of_)
      if (isPowerOf(m, k))
        return true;
    for (const std::pair<int, int> &kr : keep_mod_)
      if (m % kr.first != kr.second)
        return true;
    for (int v : explicit_)
      if (v == m)
        return true;
    return false;
  }

  bool empty() const { return powers_of_.empty() && keep_mod_.empty() && explicit_.empty(); }

private:
  static bool starts(const std::string &s, const std::string &p) { return s.compare(0, p.size(), p) == 0; }

  static bool parseInt(const std::string &s, int &out, std::string &error) {
    if (s.empty()) {
      error = "expected a number";
      return false;
    }
    size_t used = 0;
    try {
      out = std::stoi(s, &used);
    } catch (const std::exception &) {
      error = "'" + s + "' is not a number";
      return false;
    }
    if (used != s.size()) {
      error = "'" + s + "' is not a number";
      return false;
    }
    return true;
  }

  bool addPower(int k, std::string &error) {
    if (k < 2) {
      error = "power base must be at least 2";
      return false;
    }
    powers_of_.push_back(k);
    return true;
  }

  // m is a power of k, counting k^0 = 1.
  static bool isPowerOf(int m, int k) {
    if (m < 1)
      return false;
    while (m % k == 0)
      m /= k;
    return m == 1;
  }

  std::vector<int> powers_of_;
  std::vector<std::pair<int, int>> keep_mod_;
  std::vector<int> explicit_;
};
