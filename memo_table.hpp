#pragma once

#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "sequence.hpp"

// The e_m memo, shared by both searches: one eviction order across every degree. Capped at memoCap() entries or memoBytes() bytes, read
// once at construction. A miss only ever costs time, but the recomputation is charged to --max-work again.
//
// Two layouts, chosen at construction. Where m and sequence::identifier() pack together inside 64 bits, that integer is the key of a single
// table: one allocation per entry, no hashing, and -- the reason it is one table rather than one per m -- a single bucket array. A bucket
// array grows on rehash and never shrinks, so per-degree tables ratchet: each degree in turn grows its own array and keeps it after
// eviction empties it, reaching 16x the entry count on a Z_6 sweep. One array cannot do that.
//
// Rings whose packing overflows keep per-m sequence keys, where equality rather than the identifier decides membership and a collision
// costs only a probe.
template <typename R>
class MemoTable {
public:
  explicit MemoTable(int levels)
      : span(spanOf()), packed(span > 0), level_count(levels), tables(packed ? 0 : levels), cap(resolveCap()) {}

  int levels() const { return level_count; }
  size_t size() const { return entries; }
  // Entries the cap works out to, for a caller that asked for one in bytes.
  unsigned long long capacity() const { return cap; }

  // Lookups answered from the table, lookups that had to recurse, and entries dropped to stay under the cap. Lifetime tallies: a caller
  // wanting one span's share takes a difference. What they measure is whether the cap is costing recomputation or only memory.
  unsigned long long hitCount() const { return hits; }
  unsigned long long missCount() const { return misses; }
  unsigned long long evictionCount() const { return evictions; }

  // Buckets held, which is where a capped memo's memory goes once the entries themselves stop growing.
  unsigned long long bucketCount() const {
    unsigned long long n = packed_table.bucket_count();
    for (const auto &table : tables)
      n += table.bucket_count();
    return n;
  }

  // What one entry costs. Every entry is the same size, since a sequence always holds R::order ints, so a byte budget is a plain divide.
  // The per-allocation overhead is a guess at what the platform adds: four pointers, which is what measuring a full memo against its cap
  // implied, rather than the two a block header suggests. A byte cap is a target, not an exact ceiling.
  static unsigned long long bytesPerEntry() {
    const unsigned long long block = 4 * sizeof(void *);
    // A bucket is two list iterators, and the table grows in powers of two, so the array runs to twice the entries in the worst case.
    const unsigned long long buckets = 2 * (2 * sizeof(void *));
    if (spanOf() > 0) {
      // One node, plus a fixed slot in the eviction ring. The key itself needs no storage.
      unsigned long long node = sizeof(unsigned long long) + sizeof(PackedEntry) + 2 * sizeof(void *) + block;
      return node + sizeof(unsigned long long) + buckets;
    }
    unsigned long long node = sizeof(sequence<R>) + sizeof(Entry) + 2 * sizeof(void *) + block; // + next pointer and cached hash
    unsigned long long key = sizeof(int) * (unsigned long long)R::order + block;
    unsigned long long link = sizeof(typename Recency::value_type) + 2 * sizeof(void *) + block;
    return node + key + link + buckets;
  }

  // Called from a bad_alloc handler, so it must not allocate.
  void clear() {
    packed_table.clear();
    for (auto &table : tables)
      table.clear();
    recency.clear();
    slots.clear();
    hand = 0;
    entries = 0;
  }

  // Invalidated by the next insert().
  const R *find(int m, const sequence<R> &S) {
    if (packed) {
      auto it = packed_table.find(keyOf(m, S));
      if (it == packed_table.end()) {
        misses++;
        return nullptr;
      }
      hits++;
      // The whole cost of maintaining the eviction order: one byte, in a node already in cache. Exact LRU spliced a list here instead.
      it->second.ref = 1;
      return &it->second.value;
    }
    auto it = tables[m].find(S);
    if (it == tables[m].end()) {
      misses++;
      return nullptr;
    }
    hits++;
    if (cap > 0)
      recency.splice(recency.end(), recency, it->second.at);
    return &it->second.value;
  }

  void insert(int m, const sequence<R> &S, const R &value) {
    if (packed) {
      const unsigned long long key = keyOf(m, S);
      auto res = packed_table.emplace(key, PackedEntry{value, 0, 1});
      if (!res.second) {
        res.first->second.value = value;
        res.first->second.ref = 1;
        return;
      }
      entries++;
      // Cannot pick the entry just inserted, which holds no slot yet, so res survives the call.
      if (cap > 0)
        res.first->second.slot = takeSlot(key);
      return;
    }
    auto res = tables[m].emplace(S, Entry{value, typename Recency::iterator()});
    if (!res.second) {
      res.first->second.value = value;
      if (cap > 0)
        recency.splice(recency.end(), recency, res.first->second.at);
      return;
    }
    if (cap > 0) {
      // Points at the key inside the map node, which unordered_map keeps stable.
      try {
        recency.push_back({m, &res.first->first});
      } catch (...) {
        tables[m].erase(res.first);
        throw;
      }
      res.first->second.at = std::prev(recency.end());
    }
    entries++;
    while (cap > 0 && entries > cap)
      evict();
  }

private:
  using Recency = std::list<std::pair<int, const sequence<R> *>>;

  struct Entry {
    R value;
    typename Recency::iterator at;
  };

  // Second chance: ref is set on every hit and cleared as the hand passes, so an entry survives one sweep after being used. Approximate
  // where the list was exact, trading a little eviction quality for the list itself.
  struct PackedEntry {
    R value;
    unsigned slot;
    unsigned char ref;
  };

  // Degree and sequence in one integer. Injective because an identifier is below span and a degree below level_count, which is what
  // spanOf() checked before choosing this layout.
  unsigned long long keyOf(int m, const sequence<R> &S) const { return (unsigned long long)m * span + S.identifier(); }

  // The stride m is multiplied by: T_MAX() raised to order - 1, one past the largest identifier. Zero when the packing would overflow 64
  // bits, either on its own or once multiplied by the degrees a solver indexes, and the per-m layout is used instead.
  static unsigned long long spanOf() {
    const unsigned long long t = (unsigned long long)T_MAX();
    if (t < 2)
      return 0;
    unsigned long long acc = 1;
    for (int i = 1; i < R::order; i++) {
      if (acc > ~0ULL / t)
        return 0;
      acc *= t;
    }
    const unsigned long long degrees = (unsigned long long)M_MAX() + 1;
    return acc > ~0ULL / degrees ? 0 : acc;
  }

  // A budget too small for even one entry still has to cap at one: 0 would mean unlimited. Clamped so a slot index stays 32 bits, a bound
  // no reachable amount of memory comes near.
  static unsigned long long resolveCap() {
    if (memoBytes() == 0)
      return memoCap() > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : memoCap();
    unsigned long long n = memoBytes() / bytesPerEntry();
    if (n > 0xFFFFFFFFULL)
      n = 0xFFFFFFFFULL;
    return n > 0 ? n : 1;
  }

  // Claims a slot for a new entry, evicting to make room once the ring is full. The hand passes at most twice: everything it cleared on the
  // way round is unreferenced by the time it comes back.
  unsigned takeSlot(unsigned long long key) {
    if (slots.size() < cap) {
      slots.push_back(key);
      return (unsigned)(slots.size() - 1);
    }
    for (;;) {
      if (hand >= slots.size())
        hand = 0;
      auto it = packed_table.find(slots[hand]);
      if (it->second.ref) {
        it->second.ref = 0;
        hand++;
        continue;
      }
      packed_table.erase(it);
      entries--;
      evictions++;
      const size_t victim = hand++;
      slots[victim] = key;
      return (unsigned)victim;
    }
  }

  void evict() {
    // Copied: the key lives in the node erase() is about to destroy.
    sequence<R> key = *recency.front().second;
    tables[recency.front().first].erase(key);
    recency.pop_front();
    entries--;
    evictions++;
  }

  const unsigned long long span;
  const bool packed;
  const int level_count;
  std::unordered_map<unsigned long long, PackedEntry> packed_table;
  std::vector<std::unordered_map<sequence<R>, Entry>> tables;
  std::vector<unsigned long long> slots;
  Recency recency;
  const unsigned long long cap;
  size_t entries = 0;
  size_t hand = 0;
  unsigned long long hits = 0, misses = 0, evictions = 0;
};
