#pragma once

// Search bounds: m < M_MAX() and T_MIN(m) <= t < T_MAX(m). Correctness invariants, not just loop limits:
//
//   * solvers size their memo tables from M_MAX() and index them by m, so m >= M_MAX() is out of bounds.
//   * sequence::identifier() packs multiplicities as digits in base T_MAX(), so a multiplicity >= T_MAX() overflows its digit and collides
//     with a different sequence, silently corrupting memoized results.
//
// EGZ() enforces both rather than trusting the caller. Override the defaults with -DEGZ_M_MAX / -DEGZ_T_MAX, or --m-max / --t-max at run
// time. See "Search bounds" in README.md.
#ifndef EGZ_M_MAX
#define EGZ_M_MAX 20
#endif
#ifndef EGZ_T_MAX
#define EGZ_T_MAX 25
#endif

// Entries the e_m memo may hold before evicting the least recently used; 0 is unlimited.
#ifndef EGZ_MEMO_CAP
#define EGZ_MEMO_CAP 0
#endif

namespace egz {
inline int m_max = EGZ_M_MAX;
inline int t_max = EGZ_T_MAX;
// Work units one EGZ(t, m) may spend before being abandoned; 0 is unlimited. See "Bounding the work per cell" in README.md.
inline long long max_work = 0;
inline unsigned long long memo_cap = EGZ_MEMO_CAP;
// Gates the DEBUG progress tracing in egz_top_down.hpp (see --quiet).
inline bool verbose = true;
} // namespace egz

inline int M_MAX() { return egz::m_max; }
inline int T_MIN(int m = 1) { return m; }
inline int T_MAX(int = 0) { return egz::t_max; }

// Must be called before any solver is constructed: they size their memo tables from M_MAX() at construction time.
inline void setSearchBounds(int m_max, int t_max) {
  egz::m_max = m_max;
  egz::t_max = t_max;
}

inline void setVerbose(bool on) { egz::verbose = on; }

// Returned when a search gave up before reaching an answer: the work budget ran out, or it hit an internal ceiling. Distinct from 0, which
// means no EGZ constant exists. Shared by both searches, so it lives here.
constexpr int EGZ_ABANDONED = -1;

inline long long workBudget() { return egz::max_work; }
inline void setWorkBudget(long long units) { egz::max_work = units; }

inline unsigned long long memoCap() { return egz::memo_cap; }
// Must be called before any solver is constructed: MemoTable reads the cap once.
inline void setMemoCap(unsigned long long entries) { egz::memo_cap = entries; }
