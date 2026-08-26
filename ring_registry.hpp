#pragma once

#include <string>
#include <vector>

#include "rings.hpp"

// Type tag, so a ring can be handed to a visitor without being instantiated.
template <typename T>
struct ring_tag {
  using type = T;
};

template <typename... Rs>
struct ring_list {};

// Every ring the executable can be pointed at, keyed by R::name().
// To expose a new ring, implement it in rings.hpp and add it to this list.
using AllRings = ring_list<Zn<2>, Zn<3>, Zn<4>, Zn<5>, Zn<6>, Zn<7>, Zn<8>, Zn<9>, Zn<10>, Zn<11>, Zn<12>, Znp<2, 2>, Znp<2, 3>,
                           Znp<3, 2>, F4, Z_2_over, product<Zn<2>, Zn<2>>, product<Zn<2>, Zn<3>>>;

template <typename Visitor, typename... Rs>
bool dispatchRingImpl(const std::string &name, Visitor &visitor, ring_list<Rs...>) {
  bool found = false;
  // Fold over the list; the first ring whose name matches wins.
  (void)std::initializer_list<int>{(found || Rs::name() != name ? 0 : (found = true, visitor(ring_tag<Rs>{}), 0))...};
  return found;
}

// Calls visitor with a ring_tag<R> for the ring named `name`.
// Returns false if no ring has that name.
template <typename Visitor>
bool dispatchRing(const std::string &name, Visitor &&visitor) {
  return dispatchRingImpl(name, visitor, AllRings{});
}

template <typename... Rs>
std::vector<std::string> ringNamesImpl(ring_list<Rs...>) {
  return {Rs::name()...};
}

inline std::vector<std::string> ringNames() { return ringNamesImpl(AllRings{}); }
