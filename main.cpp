#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "conditional_file_stream.hpp"
#include "config.hpp"
#include "egz_bottom_up.hpp"
#include "egz_top_down.hpp"
#include "ring_registry.hpp"
#include "skip_rule.hpp"

using namespace std;

// Only the bottom-up search splits its time between e_m and the climb, so the reports below have to ask whether this Solver does. One
// that does not track it reports zero, which reportSplit() then prints as nothing.
template <typename S, typename = void>
struct tracksSplit : false_type {};
template <typename S>
struct tracksSplit<S, void_t<decltype(declval<const S &>().emMs())>> : true_type {};

template <typename S>
double emMsOf(const S &s) {
  if constexpr (tracksSplit<S>::value)
    return s.emMs();
  else
    return 0;
}

template <typename S>
double climbMsOf(const S &s) {
  if constexpr (tracksSplit<S>::value)
    return s.climbMs();
  else
    return 0;
}

static void reportSplit(const string &what, double em_ms, double climb_ms) {
  double em = em_ms / 1000.0, climb = climb_ms / 1000.0;
  if (em + climb <= 0)
    return;
  cerr << fixed << setprecision(1) << "time " << what << ": e_m " << em << " s (" << 100.0 * em / (em + climb) << "%), climb " << climb
       << " s (" << 100.0 * climb / (em + climb) << "%)" << endl;
}

// Lookups and evictions accumulate over the solver's lifetime, so a row's share of them is a difference; occupancy is read as it stands.
struct MemoCounts {
  unsigned long long hits = 0, misses = 0, evictions = 0;
};

// Both searches memoize e_m, so neither of the two below needs the detection above.
template <typename S>
MemoCounts memoCountsOf(const S &s) {
  return {s.memoHits(), s.memoMisses(), s.memoEvictions()};
}

template <typename S>
void reportMemo(const string &what, const S &s, const MemoCounts &since) {
  unsigned long long hits = s.memoHits() - since.hits, misses = s.memoMisses() - since.misses;
  unsigned long long evicted = s.memoEvictions() - since.evictions, lookups = hits + misses;
  cerr << "memo " << what << ": " << s.memoEntries() << " entries";
  if (s.memoCapacity() > 0)
    cerr << fixed << setprecision(1) << ", " << 100.0 * (double)s.memoEntries() / (double)s.memoCapacity() << "% of the "
         << s.memoCapacity() << " cap";
  else
    cerr << " (uncapped)";
  if (lookups > 0)
    cerr << fixed << setprecision(1) << "; " << lookups << " lookups, " << 100.0 * (double)hits / (double)lookups << "% hit, " << evicted
         << " evicted";
  cerr << endl;
}

// Writes one table for ring R: rows are m, columns are t, and each cell holds EGZ(t, m) - t. See "Output format" in README.md.
template <typename R, typename Solver>
void findEGZs(int m_max, int m_min, const string &out_dir, bool to_file, bool quiet, const SkipRule &skip) {
  string output_file_name = "EGZ_" + R::name() + ".tsv";
  ConditionalFileStream output_file(output_file_name, to_file, out_dir);

  // TopDownEGZSolver<R> or BottomUpEGZSolver<R>; see --method. Independent implementations that agree, not one wrapping the other.
  Solver s;

  for (int i = 1; i < T_MAX(m_max); i++)
    output_file << "\t" << i;

  vector<int> skipped;
  long long abandoned = 0;
  for (int m = m_min; m < m_max; m++) {
    // Omitted rather than blanked: an absent row says "not computed", which a blank row could not distinguish from "no EGZ constant
    // exists".
    if (skip.skips(m)) {
      skipped.push_back(m);
      continue;
    }
    // Each row is preceded by its newline, so omitting one leaves no gap and the file still ends without a trailing newline.
    output_file << "\n";
    output_file << m;
    // The split accumulates over the solver's lifetime, so a row's share of it is a difference.
    double row_em = emMsOf(s), row_climb = climbMsOf(s);
    MemoCounts row_memo = memoCountsOf(s);
    // One tab before each cell, so a row ends without a trailing tab and is exactly T_MAX(m_max) - 1 cells wide.
    for (int t = 1; t < T_MAX(m_max); t++) {
      output_file << "\t";
      // Outside this row's range of t, so nothing was computed. Blank is reserved for the one case where it is an answer, below.
      if (t < T_MIN(m) || t >= T_MAX(m)) {
        output_file << "?";
        continue;
      }
      int e = s.EGZ(t, m);
      if (e == EGZ_ABANDONED) {
        // Must come before the blank test below, where e - t is negative and would be written blank as though no constant existed.
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
    // A row takes minutes and a table hours, so flush: an interrupted run then keeps every row it finished, not whatever was past the
    // buffer.
    output_file.flush();
    reportSplit("m = " + to_string(m), emMsOf(s) - row_em, climbMsOf(s) - row_climb);
    reportMemo("m = " + to_string(m), s, row_memo);
  }

  if (abandoned > 0) {
    // The budget is not the only thing that abandons a cell -- the bottom-up search also gives up on a level too large to hold -- so naming
    // --max-work when none was set would send the reader after the wrong knob.
    cerr << "abandoned " << abandoned << " cell" << (abandoned == 1 ? "" : "s");
    if (workBudget() > 0)
      cerr << " at --max-work " << workBudget();
    else
      cerr << " (no --max-work set, so these hit an internal limit -- see \"Two searches\" in README.md)";
    cerr << "; they are written as ?" << endl;
  }
  if (!skipped.empty()) {
    cerr << "skipped " << skipped.size() << " row" << (skipped.size() == 1 ? "" : "s") << ", m =";
    for (int m : skipped)
      cerr << " " << m;
    cerr << endl;
  }
  reportSplit("total", emMsOf(s), climbMsOf(s));
  // Occupancy, not an accumulated total: what the memo held when the sweep ended.
  reportMemo("final", s, MemoCounts{});
}

static void usage() {
  cout << "usage: egz-solver [options]\n"
       << "\n"
       << "  --ring NAME     ring to compute (default: Z_2); --list-rings to see all.\n"
       << "                  Also accepts a quotient built at run time:\n"
       << "                    Z_n[vars]/(relations)   'Z_2[x]/(x^2+x+1)'\n"
       << "                                            'Z_2[x,y]/(x^2,y^2)'\n"
       << "                    Z_nvars_by_relations    the same ring, spelled the way\n"
       << "                                            it is named; no shell quoting\n"
       << "                  One relation per variable, in that variable alone, of\n"
       << "                  degree >= 1 and with a leading coefficient invertible\n"
       << "                  mod n.\n"
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
       << "  --memo-cap N    hold at most N memoized e_m values, dropping one not\n"
       << "                  read lately (0 = no limit). A K/M/G/T suffix\n"
       << "                  reads N as bytes instead (--memo-cap 8G), which is an\n"
       << "                  estimate, not an exact ceiling. A recomputed value is\n"
       << "                  charged to --max-work again.\n"
       << "  --method WHICH  bottom-up (default) or top-down: two independent\n"
       << "                  searches that agree. top-down is slower but has no\n"
       << "                  ceiling on memory. See README.md.\n"
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

// Multiplier a --memo-cap suffix stands for, or 0 for one that stands for nothing. K, M, G and T are binary, and the KB and KiB spellings
// mean the same thing; an empty suffix is the entry count, which has no multiplier of its own.
static unsigned long long sizeSuffix(string suffix) {
  for (char &c : suffix)
    c = (char)tolower((unsigned char)c);
  if (suffix.size() > 1 && suffix.back() == 'b')
    suffix.pop_back();
  if (suffix.size() > 1 && suffix.back() == 'i')
    suffix.pop_back();
  if (suffix.size() != 1)
    return 0;
  switch (suffix[0]) {
  case 'k':
    return 1ULL << 10;
  case 'm':
    return 1ULL << 20;
  case 'g':
    return 1ULL << 30;
  case 't':
    return 1ULL << 40;
  default:
    return 0;
  }
}

// --memo-cap, in whichever unit it was written: a bare number is entries, a suffixed one is bytes. 64-bit because either runs past what int
// holds, and because stoull reads a negative as its wraparound. Sets one of entries and bytes and clears the other, so the last spelling on
// the command line is the one that counts.
static void memoCapArg(int &i, int argc, char **argv, const string &flag, unsigned long long &entries, unsigned long long &bytes) {
  if (i + 1 >= argc) {
    cerr << flag << " requires a value" << endl;
    exit(2);
  }
  string raw = argv[++i];
  unsigned long long value = 0, scale = 1;
  size_t consumed = 0;
  try {
    if (raw.empty() || raw[0] == '-')
      throw invalid_argument("not a non-negative number");
    value = stoull(raw, &consumed);
    if (consumed != raw.size())
      scale = sizeSuffix(raw.substr(consumed));
    if (scale == 0)
      throw invalid_argument("unknown unit");
  } catch (const exception &) {
    cerr << flag << ": expected entries, or bytes with a K/M/G/T suffix, got '" << raw << "'" << endl;
    exit(2);
  }
  if (value > numeric_limits<unsigned long long>::max() / scale) {
    cerr << flag << ": " << raw << " does not fit in 64 bits" << endl;
    exit(2);
  }
  entries = scale == 1 ? value : 0;
  bytes = scale == 1 ? 0 : value * scale;
}

int main(int argc, char **argv) {
  string ring = "Z_2";
  string out_dir = DEFAULT_OUTPUT_DIR;
  int m_min = 1;
  int m_max = M_MAX();
  int t_max = T_MAX();
  bool to_file = true, quiet = false, bottom_up = true;
  long long max_work = 0;
  unsigned long long memo_cap = memoCap(), memo_bytes = memoBytes();
  vector<string> skip_specs;

  for (int i = 1; i < argc; i++) {
    string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (arg == "--list-rings") {
      for (const string &n : ringNames())
        cout << n << endl;
      // On stderr so that piping --list-rings still yields names alone.
      cerr << "\n--ring also accepts a quotient built at run time: Z_n[vars]/(relations),\n"
              "one relation per variable and in that variable alone. For example\n"
              "  --ring 'Z_2[x]/(x^2+x+1)'   --ring 'Z_2[x,y]/(x^2,y^2)'\n"
              "Such a ring is named Z_nvars_by_relations, which --ring accepts too and\n"
              "needs no quoting: Z_2xy_by_x2_and_y2.\n";
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
    } else if (arg == "--memo-cap") {
      memoCapArg(i, argc, argv, arg, memo_cap, memo_bytes);
    } else if (arg == "--skip") {
      if (i + 1 >= argc) {
        cerr << "--skip requires a value" << endl;
        return 2;
      }
      skip_specs.push_back(argv[++i]);
    } else if (arg == "--method") {
      if (i + 1 >= argc) {
        cerr << "--method requires a value" << endl;
        return 2;
      }
      string value = argv[++i];
      if (value == "bottom-up")
        bottom_up = true;
      else if (value == "top-down")
        bottom_up = false;
      else {
        cerr << "--method: expected top-down or bottom-up, got '" << value << "'" << endl;
        return 2;
      }
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

  // Must happen before any solver is built: they size their memo tables from M_MAX() at construction.
  setSearchBounds(m_max, t_max);
  setVerbose(!quiet);
  setWorkBudget(max_work);
  setMemoCap(memo_cap);
  setMemoBytes(memo_bytes);

  int bad_spec = 0;
  string ring_error;
  bool known = dispatchRing(
      ring,
      [&](auto tag) {
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
        if (bottom_up)
      findEGZs<R, BottomUpEGZSolver<R>>(m_max, m_min, out_dir, to_file, quiet, skip);
    else
      findEGZs<R, TopDownEGZSolver<R>>(m_max, m_min, out_dir, to_file, quiet, skip);
      },
      &ring_error);
  if (!known) {
    // A failed quotient spec gets its own message; anything else is just a name nothing answers to.
    if (!ring_error.empty())
      cerr << "--ring " << ring << ": " << ring_error << endl;
    else
      cerr << "unknown ring: " << ring << " (try --list-rings)" << endl;
    return 2;
  }
  return bad_spec;
}
