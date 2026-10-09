// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the disk cache: read/write, budget, LRU order, index
// recovery at startup, failures.

#include <string>

#include "memory_file_store.h"
#include "test_framework.h"
#include "util/disk_cache.h"

using util::DiskCache;

static void test_basic() {
  MemoryFileStore fs;
  DiskCache cache(&fs, "/c/posters", 1000);
  CHECK(cache.init());
  std::string out;
  CHECK(!cache.get("a", &out));
  cache.put("a", std::string(100, 'a'));
  CHECK(cache.get("a", &out) && out == std::string(100, 'a'));
  CHECK_EQ(cache.total_bytes(), 100u);
  cache.put("a", std::string(50, 'b'));  // replacement: size updated
  CHECK_EQ(cache.total_bytes(), 50u);
  CHECK_EQ(cache.entry_count(), 1u);
  CHECK(!fs.files["/c/posters/a.img"].private_file);
  cache.remove("a");
  CHECK(!cache.get("a", &out));
  CHECK_EQ(cache.total_bytes(), 0u);
  cache.put("trop-gros", std::string(2000, 'x'));  // bigger than the budget
  CHECK_EQ(cache.entry_count(), 0u);
}

static void test_file_names() {
  CHECK(DiskCache::file_name_for("abc_DEF-09") == "abc_DEF-09.img");
  CHECK(DiskCache::file_name_for("../../etc/passwd") == "______etc_passwd.img");
  CHECK(DiskCache::file_name_for("a b/c") == "a_b_c.img");
}

static void test_lru_eviction() {
  MemoryFileStore fs;
  DiskCache cache(&fs, "/c", 300);
  CHECK(cache.init());
  cache.put("a", std::string(100, 'a'));
  cache.put("b", std::string(100, 'b'));
  cache.put("c", std::string(100, 'c'));
  std::string out;
  CHECK(cache.get("a", &out));  // a becomes the most recent again
  cache.put("d", std::string(100, 'd'));  // evicts b (the least recent)
  CHECK(cache.get("a", &out));
  CHECK(!cache.get("b", &out));
  CHECK(cache.get("c", &out));
  CHECK(cache.get("d", &out));
  CHECK(cache.total_bytes() <= 300u);
  CHECK(fs.files.find("/c/b.img") == fs.files.end());  // file deleted
}

static void test_reload_index() {
  MemoryFileStore fs;
  {
    DiskCache cache(&fs, "/c", 1000);
    CHECK(cache.init());
    cache.put("old", std::string(400, 'o'));
    cache.put("new", std::string(400, 'n'));
    std::string out;
    CHECK(cache.get("old", &out));  // touch: old becomes the most recent
  }
  fs.files["/c/partiel.img.tmp42"].data = "x";  // interrupted write: ignored
  // Restart with a smaller budget: the order comes from the dates (touch).
  DiskCache cache(&fs, "/c", 500);
  CHECK(cache.init());
  CHECK_EQ(cache.entry_count(), 1u);
  std::string out;
  CHECK(cache.get("old", &out));
  CHECK(!cache.get("new", &out));
}

static void test_failures() {
  MemoryFileStore fs;
  fs.fail_dirs = true;
  DiskCache inactive(&fs, "/c", 1000);
  CHECK(!inactive.init());
  inactive.put("a", "data");  // no effect, no crash
  std::string out;
  CHECK(!inactive.get("a", &out));

  MemoryFileStore fs2;
  DiskCache cache(&fs2, "/c", 1000);
  CHECK(cache.init());
  fs2.fail_writes = true;
  cache.put("a", "data");
  CHECK_EQ(cache.entry_count(), 0u);  // failed write: nothing indexed
  fs2.fail_writes = false;
  cache.put("b", "data");
  fs2.files.erase("/c/b.img");  // file vanished behind the cache
  CHECK(!cache.get("b", &out));
  CHECK_EQ(cache.entry_count(), 0u);
}

int main() {
  test_basic();
  test_file_names();
  test_lru_eviction();
  test_reload_index();
  test_failures();
  return testfw::test_failures();
}
