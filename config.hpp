#pragma once

inline long long smallestPowerBiggerThan(long long base, long long value) {
  long long i = 1;
  while (i <= value) {
    i *= base;
  }
  return i;
}

// Search bounds: all values of m < M_MAX() and T_MIN(m) <= t < T_MAX(m).
//
// These are correctness invariants, not just loop limits:
//
//   * EGZSolver sizes its memo tables from M_MAX() and indexes them by m, so
//     m >= M_MAX() is out of bounds.
//   * sequence::identifier() packs element multiplicities as digits in base
//     T_MAX(). A multiplicity >= T_MAX() overflows its digit and collides with
//     a different sequence, which silently corrupts memoized results.
//
// EGZSolver::EGZ enforces both at run time rather than trusting the caller.
// The compiled-in defaults can be overridden with -DEGZ_M_MAX / -DEGZ_T_MAX,
// and at run time with --m-max / --t-max. See "Search bounds" in README.md.
#ifndef EGZ_M_MAX
#define EGZ_M_MAX 20
#endif
#ifndef EGZ_T_MAX
#define EGZ_T_MAX 25
#endif

namespace egz {
inline int m_max = EGZ_M_MAX;
inline int t_max = EGZ_T_MAX;
// Gates the DEBUG progress tracing in egz_solver.hpp (see --quiet).
inline bool verbose = true;
} // namespace egz

inline int M_MAX() { return egz::m_max; }
inline int T_MIN(int m = 1) { return m; }
inline int T_MAX(int = 0) { return egz::t_max; }

// Must be called before any EGZSolver is constructed: solvers size their memo
// tables from M_MAX() at construction time.
inline void setSearchBounds(int m_max, int t_max) {
  egz::m_max = m_max;
  egz::t_max = t_max;
}

inline void setVerbose(bool on) { egz::verbose = on; }
