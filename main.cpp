#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "conditional_file_stream.hpp"
#include "config.hpp"
#include "egz_solver.hpp"
#include "ring_registry.hpp"
#include "skip_rule.hpp"

using namespace std;

// Writes one table for ring R: rows are m, columns are t, and each cell holds
// EGZ(t, m) - t. See "Output format" in README.md.
template <typename R>
void findEGZs(int m_max, int m_min, const string &out_dir, bool to_file, bool quiet, const SkipRule &skip) {
  string output_file_name = "EGZ_" + R::name() + ".tsv";
  ConditionalFileStream output_file(output_file_name, to_file, out_dir);

  EGZSolver<R> s;

  for (int i = 1; i < T_MAX(m_max); i++)
    output_file << "\t" << i;

  vector<int> skipped;
  long long abandoned = 0;
  for (int m = m_min; m < m_max; m++) {
    // Skipped rows are omitted, not blanked: an absent row says "not computed",
    // which a blank row could not distinguish from "no EGZ constant exists".
    if (skip.skips(m)) {
      skipped.push_back(m);
      continue;
    }
    // Every row is preceded by its newline, so omitting one leaves no gap and
    // the file still ends without a trailing newline.
    output_file << "\n";
    output_file << m;
    // One tab per cell, written before it, so the row ends without a trailing
    // tab and every row is exactly T_MAX(m_max) - 1 cells wide.
    for (int t = 1; t < T_MAX(m_max); t++) {
      output_file << "\t";
      // Outside this row's range of t, so nothing was computed here. Blank is
      // reserved for the one case where a blank is an answer, below.
      if (t < T_MIN(m) || t >= T_MAX(m)) {
        output_file << "?";
        continue;
      }
      int e = s.EGZ(t, m);
      if (e == EGZ_ABANDONED) {
        // Must be caught before the blank test below: e - t would be negative
        // there, and the cell would be written blank as though no constant
        // existed. "?" says the budget ran out instead.
        abandoned++;
        output_file << "?";
        continue;
      }
      if (e - t <= -1)
        continue; // no EGZ constant for this (t, m): the sole meaning of blank
      if (!quiet) {
        cout << "EGZ(" << t << ", " << R::name() << ", " << m << ") = " << e << endl;
        cout << "EGZ-t = " << e - t << endl;
        cout << endl;
      }
      output_file << e - t;
    }
  }

  if (abandoned > 0)
    cerr << "abandoned " << abandoned << " cell" << (abandoned == 1 ? "" : "s") << " at --max-work " << workBudget()
         << "; they are written as ?" << endl;
  if (!skipped.empty()) {
    cerr << "skipped " << skipped.size() << " row" << (skipped.size() == 1 ? "" : "s") << ", m =";
    for (int m : skipped)
      cerr << " " << m;
    cerr << endl;
  }
}

static void usage() {
  cout << "usage: egz-solver [options]\n"
       << "\n"
       << "  --ring NAME     ring to compute (default: Z_2); --list-rings to see all\n"
       << "  --m-min N       first m to compute (default: 1)\n"
       << "  --m-max N       exclusive upper bound on m (default: " << M_MAX() << ")\n"
       << "  --t-max N       exclusive upper bound on t (default: " << T_MAX() << ")\n"
       << "  --out-dir DIR   directory for the .tsv (default: " << DEFAULT_OUTPUT_DIR << ")\n"
       << "  --skip EXPR     leave rows out of the table; may be repeated:\n"
       << "                    powers      m that are powers of the ring's order\n"
       << "                    pow:K       m that are powers of K\n"
       << "                    mod:K=R     keep only m congruent to R modulo K\n"
       << "                    list:a,b,c  exactly these m\n"
       << "                  Skipped rows are omitted, not written blank.\n"
       << "  --max-work N    give up on a cell after N work units (0 = no limit).\n"
       << "                  Abandoned cells are written as ?, never left blank.\n"
       << "  --no-file       print progress only, do not write a table\n"
       << "  --quiet         suppress per-value progress output\n"
       << "  --list-rings    list supported ring names and exit\n"
       << "  -h, --help      show this message\n"
       << "\n"
       << "--t-max is also the radix used to hash sequences, so it must exceed every\n"
       << "multiplicity the search reaches, not just the t values you want. The solver\n"
       << "stops with an error rather than return a wrong answer if it is too small.\n";
}

// Parses the integer argument for `flag`, advancing i past it.
static int intArg(int &i, int argc, char **argv, const string &flag) {
  if (i + 1 >= argc) {
    cerr << flag << " requires a value" << endl;
    exit(2);
  }
  string raw = argv[++i];
  try {
    size_t consumed = 0;
    int value = stoi(raw, &consumed);
    if (consumed != raw.size())
      throw invalid_argument("trailing characters");
    return value;
  } catch (const exception &) {
    cerr << flag << ": expected an integer, got '" << raw << "'" << endl;
    exit(2);
  }
}

int main(int argc, char **argv) {
  string ring = "Z_2";
  string out_dir = DEFAULT_OUTPUT_DIR;
  int m_min = 1;
  int m_max = M_MAX();
  int t_max = T_MAX();
  bool to_file = true, quiet = false;
  long long max_work = 0;
  vector<string> skip_specs;

  for (int i = 1; i < argc; i++) {
    string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (arg == "--list-rings") {
      for (const string &n : ringNames())
        cout << n << endl;
      return 0;
    } else if (arg == "--ring") {
      if (i + 1 >= argc) {
        cerr << "--ring requires a value" << endl;
        return 2;
      }
      ring = argv[++i];
    } else if (arg == "--out-dir") {
      if (i + 1 >= argc) {
        cerr << "--out-dir requires a value" << endl;
        return 2;
      }
      out_dir = argv[++i];
    } else if (arg == "--m-min") {
      m_min = intArg(i, argc, argv, arg);
    } else if (arg == "--m-max") {
      m_max = intArg(i, argc, argv, arg);
    } else if (arg == "--t-max") {
      t_max = intArg(i, argc, argv, arg);
    } else if (arg == "--max-work") {
      max_work = intArg(i, argc, argv, arg);
    } else if (arg == "--skip") {
      if (i + 1 >= argc) {
        cerr << "--skip requires a value" << endl;
        return 2;
      }
      skip_specs.push_back(argv[++i]);
    } else if (arg == "--no-file") {
      to_file = false;
    } else if (arg == "--quiet") {
      quiet = true;
    } else {
      cerr << "unknown option: " << arg << endl << endl;
      usage();
      return 2;
    }
  }

  if (m_min < 1 || m_max <= m_min) {
    cerr << "need 1 <= --m-min < --m-max (got " << m_min << " and " << m_max << ")" << endl;
    return 2;
  }
  if (t_max < 2) {
    cerr << "--t-max must be at least 2 (got " << t_max << ")" << endl;
    return 2;
  }

  // Must happen before any EGZSolver is built: solvers size their memo tables
  // from M_MAX() at construction.
  setSearchBounds(m_max, t_max);
  setVerbose(!quiet);
  setWorkBudget(max_work);

  int bad_spec = 0;
  bool known = dispatchRing(ring, [&](auto tag) {
    using R = typename decltype(tag)::type;
    SkipRule skip;
    for (const string &spec : skip_specs) {
      string error;
      if (!skip.add(spec, R::order, error)) {
        cerr << "--skip " << spec << ": " << error << endl;
        bad_spec = 2;
        return;
      }
    }
    findEGZs<R>(m_max, m_min, out_dir, to_file, quiet, skip);
  });
  if (!known) {
    cerr << "unknown ring: " << ring << " (try --list-rings)" << endl;
    return 2;
  }
  return bad_spec;
}
