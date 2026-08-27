#pragma once

#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "sequence.hpp"

// The e_m memo, shared by both searches: one table per m, one eviction order across all of them. Capped at memoCap() entries or memoBytes()
// bytes, read once at construction. A miss only ever costs time, but the recomputation is charged to --max-work again.
//
// Two layouts, chosen at construction. Where sequence::identifier() is injective -- its base-T_MAX() packing stays inside 64 bits -- that
// integer is the key, costing one allocation per entry rather than three. Rings whose packing overflows keep sequence keys, where equality
// rather than the identifier decides membership and a collision costs only a probe.
template <typename R>
class MemoTable {
public:
  explicit MemoTable(int levels)
      : packed(packs()), level_count(levels), packed_tables(packed ? levels : 0), tables(packed ? 0 : levels), cap(resolveCap()) {}

  int levels() const { return level_count; }
  size_t size() const { return entries; }
  // Entries the cap works out to, for a caller that asked for one in bytes.
  unsigned long long capacity() const { return cap; }

  // Lookups answered from the table, lookups that had to recurse, and entries dropped to stay under the cap. Lifetime tallies: a caller
  // wanting one span's share takes a difference. What they measure is whether the cap is costing recomputation or only memory.
  unsigned long long hitCount() const { return hits; }
  unsigned long long missCount() const { return misses; }
  unsigned long long evictionCount() const { return evictions; }

  // What one entry costs. Every entry is the same size, since a sequence always holds R::order ints, so a byte budget is a plain divide.
  // The per-allocation overhead is a guess at what the platform adds: four pointers, which is what measuring a full memo against its cap
  // implied, rather than the two a block header suggests. A byte cap is a target, not an exact ceiling.
  static unsigned long long bytesPerEntry() {
    const unsigned long long block = 4 * sizeof(void *);
    if (packs()) {
      // One node, plus a fixed slot in the eviction ring. The key itself needs no storage.
      unsigned long long node = sizeof(unsigned long long) + sizeof(PackedEntry) + 2 * sizeof(void *) + block;
      return node + sizeof(Slot) + sizeof(void *);
    }
    unsigned long long node = sizeof(sequence<R>) + sizeof(Entry) + 2 * sizeof(void *) + block; // + next pointer and cached hash
    unsigned long long key = sizeof(int) * (unsigned long long)R::order + block;
    unsigned long long link = sizeof(typename Recency::value_type) + 2 * sizeof(void *) + block;
    return node + key + link + sizeof(void *);
  }

  // Called from a bad_alloc handler, so it must not allocate.
  void clear() {
    for (auto &table : packed_tables)
      table.clear();
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
      auto it = packed_tables[m].find(S.identifier());
      if (it == packed_tables[m].end()) {
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
      const unsigned long long key = S.identifier();
      auto res = packed_tables[m].emplace(key, PackedEntry{value, 0, 1});
      if (!res.second) {
        res.first->second.value = value;
        res.first->second.ref = 1;
        return;
      }
      entries++;
      // Cannot pick the entry just inserted, which holds no slot yet, so res survives the call.
      if (cap > 0)
        res.first->second.slot = takeSlot(m, key);
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

  // One per live entry, indexed by PackedEntry::slot. A vector, so the eviction order costs no allocation of its own.
  struct Slot {
    int m;
    unsigned long long key;
  };

  // Whether the base-T_MAX() packing of order - 1 multiplicities stays inside 64 bits. If it does, the identifier is injective and can be
  // the key; if it wraps, two different sequences can share one and only operator== tells them apart.
  static bool packs() {
    const unsigned long long t = (unsigned long long)T_MAX();
    if (t < 2)
      return false;
    unsigned long long acc = 1;
    for (int i = 1; i < R::order; i++) {
      if (acc > ~0ULL / t)
        return false;
      acc *= t;
    }
    return true;
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
  unsigned takeSlot(int m, unsigned long long key) {
    if (slots.size() < cap) {
      slots.push_back({m, key});
      return (unsigned)(slots.size() - 1);
    }
    for (;;) {
      if (hand >= slots.size())
        hand = 0;
      auto it = packed_tables[slots[hand].m].find(slots[hand].key);
      if (it->second.ref) {
        it->second.ref = 0;
        hand++;
        continue;
      }
      packed_tables[slots[hand].m].erase(it);
      entries--;
      evictions++;
      const size_t victim = hand++;
      slots[victim] = {m, key};
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

  const bool packed;
  const int level_count;
  std::vector<std::unordered_map<unsigned long long, PackedEntry>> packed_tables;
  std::vector<std::unordered_map<sequence<R>, Entry>> tables;
  std::vector<Slot> slots;
  Recency recency;
  const unsigned long long cap;
  size_t entries = 0;
  size_t hand = 0;
  unsigned long long hits = 0, misses = 0, evictions = 0;
};
