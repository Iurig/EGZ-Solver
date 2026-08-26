// Dumps a ring's complete structure: order, characteristic, unit, and the full
// addition and multiplication tables.
//
//   dump_ring --list          list every ring in the registry
//   dump_ring <ring-name>     dump that ring
//   dump_ring --all           dump every ring, in registry order
//
// This makes "is the new implementation the same ring?" checkable.
// tests/ring_goldens.tsv holds a dump of each ring as it stands and
// tests/test_rings.py compares against it, so a ring reimplemented behind the
// same name -- or one meant to recover an existing ring -- must reproduce its
// tables exactly.
#include <iostream>
#include <string>

#include "ring_registry.hpp"

template <typename R>
static void dump() {
  std::cout << "name\t" << R::name() << "\n";
  std::cout << "order\t" << R::order << "\n";
  std::cout << "characteristic\t" << R::characteristic << "\n";
  std::cout << "unit\t" << R::unit << "\n";
  // Elements are their own indices: every ring is constructed from an int in
  // [0, order) and exposes it as `value`. sequence.hpp indexes storage by it.
  for (int i = 0; i < R::order; i++) {
    std::cout << "add\t" << i;
    for (int j = 0; j < R::order; j++) {
      // Non-const: Znp declares operator+ and operator* without const.
      R a(i), b(j);
      std::cout << "\t" << (a + b).value;
    }
    std::cout << "\n";
  }
  for (int i = 0; i < R::order; i++) {
    std::cout << "mul\t" << i;
    for (int j = 0; j < R::order; j++) {
      R a(i), b(j);
      std::cout << "\t" << (a * b).value;
    }
    std::cout << "\n";
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: dump_ring [--list | --all | <ring-name>]" << std::endl;
    return 2;
  }
  std::string arg = argv[1];
  if (arg == "--list") {
    for (const std::string &n : ringNames())
      std::cout << n << std::endl;
    return 0;
  }
  if (arg == "--all") {
    for (const std::string &n : ringNames())
      dispatchRing(n, [](auto tag) { dump<typename decltype(tag)::type>(); });
    return 0;
  }
  std::string error;
  if (!dispatchRing(
          arg, [](auto tag) { dump<typename decltype(tag)::type>(); }, &error)) {
    if (!error.empty())
      std::cerr << arg << ": " << error << std::endl;
    else
      std::cerr << "unknown ring: " << arg << " (try --list)" << std::endl;
    return 2;
  }
  return 0;
}
