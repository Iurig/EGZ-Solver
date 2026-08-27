#pragma once

#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "sequence.hpp"

// The e_m memo, shared by both searches: one table per m, one eviction order across all of them. Capped at memoCap() entries or memoBytes()
// bytes, read once at construction, the least recently used is dropped. A miss only ever costs time, but the recomputation is charged to
// --max-work again.
template <typename R>
class MemoTable {
public:
  explicit MemoTable(int levels) : tables(levels), cap(resolveCap()) {}

  int levels() const { return (int)tables.size(); }
  size_t size() const { return entries; }
  // Entries the cap works out to, for a caller that asked for one in bytes.
  unsigned long long capacity() const { return cap; }

  // What one entry costs: a map node, the vector<int> its key owns, a bucket slot at load factor 1, and a recency node. Every entry is this
  // same size, since a sequence always holds R::order ints, so a byte budget is a plain divide. The per-allocation overhead is a guess at
  // what the platform's allocator adds, which makes a byte cap a target rather than an exact ceiling.
  static unsigned long long bytesPerEntry() {
    const unsigned long long block = 2 * sizeof(void *);
    unsigned long long node = sizeof(sequence<R>) + sizeof(Entry) + 2 * sizeof(void *) + block; // + next pointer and cached hash
    unsigned long long key = sizeof(int) * (unsigned long long)R::order + block;
    unsigned long long link = sizeof(typename Recency::value_type) + 2 * sizeof(void *) + block;
    return node + key + link + sizeof(void *);
  }

  // Called from a bad_alloc handler, so it must not allocate.
  void clear() {
    for (auto &table : tables)
      table.clear();
    recency.clear();
    entries = 0;
  }

  // Invalidated by the next insert().
  const R *find(int m, const sequence<R> &S) {
    auto it = tables[m].find(S);
    if (it == tables[m].end())
      return nullptr;
    if (cap > 0)
      recency.splice(recency.end(), recency, it->second.at);
    return &it->second.value;
  }

  void insert(int m, const sequence<R> &S, const R &value) {
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

  // A budget too small for even one entry still has to cap at one: 0 would mean unlimited.
  static unsigned long long resolveCap() {
    if (memoBytes() == 0)
      return memoCap();
    unsigned long long n = memoBytes() / bytesPerEntry();
    return n > 0 ? n : 1;
  }

  void evict() {
    // Copied: the key lives in the node erase() is about to destroy.
    sequence<R> key = *recency.front().second;
    tables[recency.front().first].erase(key);
    recency.pop_front();
    entries--;
  }

  std::vector<std::unordered_map<sequence<R>, Entry>> tables;
  Recency recency;
  const unsigned long long cap;
  size_t entries = 0;
};
