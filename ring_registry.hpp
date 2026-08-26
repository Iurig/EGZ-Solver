#pragma once

#include <string>
#include <vector>

#include "quotient.hpp"
#include "rings.hpp"

// Type tag, so a ring can be handed to a visitor without being instantiated.
template <typename T>
struct ring_tag {
  using type = T;
};

template <typename... Rs>
struct ring_list {};

// Every ring the executable can be pointed at, keyed by R::name(). To add one,
// implement it in rings.hpp and put it in this list.
using AllRings = ring_list<Zn<2>, Zn<3>, Zn<4>, Zn<5>, Zn<6>, Zn<7>, Zn<8>, Zn<9>, Zn<10>, Zn<11>, Zn<12>, Znp<2, 2>, Znp<2, 3>,
                           Znp<3, 2>, F4, Z_2_over, product<Zn<2>, Zn<2>>, product<Zn<2>, Zn<3>>>;

template <typename Visitor, typename... Rs>
bool dispatchRingImpl(const std::string &name, Visitor &visitor, ring_list<Rs...>) {
  bool found = false;
  // Fold over the list; the first ring whose name matches wins.
  (void)std::initializer_list<int>{(found || Rs::name() != name ? 0 : (found = true, visitor(ring_tag<Rs>{}), 0))...};
  return found;
}

// Calls visitor with a ring_tag<R> for the ring `name` denotes: one compiled in
// above, or a quotient spec such as Z_2[x]/(x^2+x+1), built at run time and
// visited as ring_tag<Quotient>. False if it is neither -- with `error` set
// when it is a malformed spec rather than an unknown name.
template <typename Visitor>
bool dispatchRing(const std::string &name, Visitor &&visitor, std::string *error = nullptr) {
  // Compiled-in rings win: Z_2x_by_x2 is both a registered name and a valid
  // spec, and the registered one made the published table.
  if (dispatchRingImpl(name, visitor, AllRings{}))
    return true;
  std::string why;
  if (configureQuotient(name, why)) {
    visitor(ring_tag<Quotient>{});
    return true;
  }
  if (error && looksLikeQuotient(name))
    *error = why;
  return false;
}

template <typename... Rs>
std::vector<std::string> ringNamesImpl(ring_list<Rs...>) {
  return {Rs::name()...};
}

inline std::vector<std::string> ringNames() { return ringNamesImpl(AllRings{}); }
