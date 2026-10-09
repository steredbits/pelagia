// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_DISK_CACHE_H
#define PELAGIA_CORE_UTIL_DISK_CACHE_H

// Disk cache of binary objects (posters as served by Jellyfin)
// with a byte budget and LRU eviction. The access order is kept in memory
// while running and rebuilt from the modification dates at startup
// (each read "touches" the file). Thread-safe: called by the worker
// threads.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "util/file_store.h"

namespace util {

class DiskCache {
 public:
  DiskCache(FileStore* store, std::string dir, uint64_t budget_bytes);

  // Creates the folder and indexes its content (then eviction if the budget
  // decreased). False if the folder is unusable: the cache is then inactive
  // (get fails, put does nothing), the application works without it.
  bool init();

  bool get(const std::string& key, std::string* out);
  void put(const std::string& key, const std::string& data);
  void remove(const std::string& key);

  uint64_t total_bytes() const;
  size_t entry_count() const;

  // Safe file name derived from the key (characters outside [A-Za-z0-9_-]
  // replaced), ".img" suffix. Pure function.
  static std::string file_name_for(const std::string& key);

 private:
  struct Entry {
    uint64_t size = 0;
    uint64_t order = 0;  // smaller = less recently used
  };

  void evict_locked();
  std::string path_for(const std::string& file_name) const;

  FileStore* store_;
  std::string dir_;
  uint64_t budget_;
  bool ready_ = false;
  mutable std::mutex mutex_;
  std::map<std::string, Entry> entries_;  // by file name
  uint64_t total_ = 0;
  uint64_t next_order_ = 1;
};

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_DISK_CACHE_H
