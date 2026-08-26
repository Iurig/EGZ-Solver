// Development tool: replays cells from an existing table in experimental-tables/
// against the current solver, timing each one. Used to generate the regression
// fixture in tests/regression_cases.tsv -- see tests/README.md.
//
//   table_probe <ring-name> <tsv-path> <budget-ms>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "egz_solver.hpp"
#include "ring_registry.hpp"

struct Cell {
  int m, t, expected_egz;
};

// Splits on tabs, keeping empty fields (a blank cell is meaningful).
static std::vector<std::string> splitTabs(std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
    line.pop_back();
  std::vector<std::string> out;
  std::string cur;
  std::istringstream ss(line);
  while (std::getline(ss, cur, '\t'))
    out.push_back(cur);
  return out;
}

// A cell at row m, column t holds e - t, so EGZ(t, m) == value + t.
static std::vector<Cell> readTable(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open " << path << std::endl;
    exit(2);
  }
  std::string line;
  std::getline(in, line);
  std::vector<std::string> header = splitTabs(line);
  std::vector<Cell> cells;
  while (std::getline(in, line)) {
    std::vector<std::string> row = splitTabs(line);
    if (row.empty() || row[0].empty())
      continue;
    int m = std::stoi(row[0]);
    for (size_t i = 1; i < row.size() && i < header.size(); i++) {
      if (row[i].empty())
        continue; // blank: outside [T_MIN, T_MAX) for this row, or e - t <= -1
      int t = std::stoi(header[i]);
      cells.push_back({m, t, std::stoi(row[i]) + t});
    }
  }
  return cells;
}

template <typename R>
void probe(const std::vector<Cell> &cells, double budget_ms) {
  EGZSolver<R> solver;
  int last_m = -1;
  bool row_over_budget = false;
  for (const Cell &c : cells) {
    if (c.m != last_m) {
      last_m = c.m;
      row_over_budget = false;
    }
    if (row_over_budget)
      continue; // cost grows with t, so skip the rest of this row
    auto start = std::chrono::steady_clock::now();
    int actual = solver.EGZ(c.t, c.m);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << (actual == c.expected_egz ? "MATCH" : "MISMATCH") << "\t" << R::name() << "\t" << c.m << "\t" << c.t << "\t"
              << c.expected_egz << "\t" << actual << "\t" << (long)ms << std::endl;
    if (ms > budget_ms)
      row_over_budget = true;
  }
}

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cerr << "usage: table_probe <ring-name> <tsv-path> <budget-ms>" << std::endl;
    return 2;
  }
  std::string ring = argv[1];
  std::vector<Cell> cells = readTable(argv[2]);
  double budget = std::stod(argv[3]);
  if (!dispatchRing(ring, [&](auto tag) { probe<typename decltype(tag)::type>(cells, budget); })) {
    std::cerr << "unknown ring: " << ring << std::endl;
    return 2;
  }
  return 0;
}
