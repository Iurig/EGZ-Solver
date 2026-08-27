#pragma once

#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "sequence.hpp"

// The e_m memo, shared by both searches: one table per m, one eviction order across all of them. Capped at memoCap() entries, read once at
// construction, the least recently used is dropped. A miss only ever costs time, but the recomputation is charged to --max-work again.
template <typename R>
class MemoTable {
public:
  explicit MemoTable(int levels) : tables(levels), cap(memoCap()) {}

  int levels() const { return (int)tables.size(); }
  size_t size() const { return entries; }

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
