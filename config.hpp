#pragma once

// Search bounds: all values of m < M_MAX() and T_MIN(m) <= t < T_MAX(m).
//
// These are correctness invariants, not just loop limits:
//
//   * TopDownEGZSolver sizes its memo tables from M_MAX() and indexes them by m, so
//     m >= M_MAX() is out of bounds.
//   * sequence::identifier() packs element multiplicities as digits in base
//     T_MAX(). A multiplicity >= T_MAX() overflows its digit and collides with
//     a different sequence, which silently corrupts memoized results.
//
// TopDownEGZSolver::EGZ enforces both at run time rather than trusting the caller.
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
// Work units a single EGZ(t, m) may spend before being abandoned; 0 is
// unlimited. See "Bounding the work per cell" in README.md.
inline long long max_work = 0;
// Gates the DEBUG progress tracing in egz_top_down.hpp (see --quiet).
inline bool verbose = true;
} // namespace egz

inline int M_MAX() { return egz::m_max; }
inline int T_MIN(int m = 1) { return m; }
inline int T_MAX(int = 0) { return egz::t_max; }

// Must be called before any TopDownEGZSolver is constructed: solvers size their memo
// tables from M_MAX() at construction time.
inline void setSearchBounds(int m_max, int t_max) {
  egz::m_max = m_max;
  egz::t_max = t_max;
}

inline void setVerbose(bool on) { egz::verbose = on; }

// Returned by EGZ(t, m) when a search gave up before reaching an answer: the
// work budget ran out, or it hit an internal ceiling. Distinct from 0, which
// means no EGZ constant exists. Shared by both searches, so it lives here
// rather than in either of them.
constexpr int EGZ_ABANDONED = -1;

inline long long workBudget() { return egz::max_work; }
inline void setWorkBudget(long long units) { egz::max_work = units; }
