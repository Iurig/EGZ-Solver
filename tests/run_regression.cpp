// Regression suite: replays known-good EGZ values taken from the published
// tables in Experimental tables/ and fails if the solver disagrees.
//
//   run_regression [path-to-regression_cases.tsv] [--method bottom-up]
//
// --method picks which implementation replays the fixture. Both must reproduce
// the published values; see "Two searches" in README.md.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "egz_bottom_up.hpp"
#include "egz_solver.hpp"
#include "ring_registry.hpp"

struct Case {
  std::string ring;
  int m, t, expected;
};

static std::vector<Case> loadCases(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open fixture: " << path << std::endl;
    exit(2);
  }
  std::vector<Case> cases;
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
      line.pop_back();
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream ss(line);
    Case c;
    std::string m, t, e;
    if (!std::getline(ss, c.ring, '\t') || !std::getline(ss, m, '\t') || !std::getline(ss, t, '\t') || !std::getline(ss, e, '\t'))
      continue;
    c.m = std::stoi(m);
    c.t = std::stoi(t);
    c.expected = std::stoi(e);
    cases.push_back(c);
  }
  return cases;
}

// Replays the fixture with one implementation. Templated on the solver so the
// two are exercised by identical code -- any difference in the result is a
// difference between the searches, not between two copies of the harness.
template <template <typename> class Solver>
static int replay(const std::map<std::string, std::vector<Case>> &byRing, size_t total) {
  int passed = 0, failed = 0, skipped = 0;
  for (const auto &entry : byRing) {
    const std::string &ring = entry.first;
    bool known = dispatchRing(ring, [&](auto tag) {
      using R = typename decltype(tag)::type;
      Solver<R> solver;
      for (const Case &c : entry.second) {
        // Skip rather than let EGZ's guard abort the whole suite.
        if (c.m >= M_MAX() || c.t >= T_MAX()) {
          std::cout << "SKIP  " << ring << " m=" << c.m << " t=" << c.t << " (needs --m-max>" << c.m << " --t-max>" << c.t << ")"
                    << std::endl;
          skipped++;
          continue;
        }
        int actual = solver.EGZ(c.t, c.m);
        if (actual == c.expected) {
          passed++;
        } else {
          failed++;
          std::cout << "FAIL  " << ring << " EGZ(t=" << c.t << ", m=" << c.m << ") expected " << c.expected << ", got " << actual
                    << std::endl;
        }
      }
    });
    if (!known) {
      std::cerr << "FAIL  unknown ring in fixture: " << ring << std::endl;
      failed++;
    }
  }

  std::cout << std::endl
            << passed << " passed, " << failed << " failed, " << skipped << " skipped (" << total << " cases)" << std::endl;
  return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  std::string path = "tests/regression_cases.tsv";
  bool bottomUp = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--method") {
      if (i + 1 >= argc) {
        std::cerr << "--method requires a value" << std::endl;
        return 2;
      }
      std::string value = argv[++i];
      if (value == "bottom-up")
        bottomUp = true;
      else if (value != "top-down") {
        std::cerr << "--method: expected top-down or bottom-up, got '" << value << "'" << std::endl;
        return 2;
      }
    } else {
      path = arg;
    }
  }

  std::vector<Case> cases = loadCases(path);
  if (cases.empty()) {
    std::cerr << "fixture contained no cases" << std::endl;
    return 2;
  }

  // Group by ring so each ring's solver (and its memo tables) is built once.
  std::map<std::string, std::vector<Case>> byRing;
  for (const Case &c : cases)
    byRing[c.ring].push_back(c);

  std::cout << "replaying with the " << (bottomUp ? "bottom-up" : "top-down") << " search" << std::endl;
  return bottomUp ? replay<BottomUpEGZSolver>(byRing, cases.size()) : replay<EGZSolver>(byRing, cases.size());
}
