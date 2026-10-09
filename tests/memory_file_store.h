// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_TESTS_MEMORY_FILE_STORE_H
#define PELAGIA_TESTS_MEMORY_FILE_STORE_H

// In-memory FileStore for the tests: modification dates driven by a
// counter (deterministic LRU order), injectable failures.

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "util/file_store.h"

class MemoryFileStore final : public util::FileStore {
 public:
  struct File {
    std::string data;
    int64_t mtime = 0;
    bool private_file = false;
  };

  bool make_dirs(const std::string& path) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fail_dirs) return false;
    dirs_.push_back(path);
    return true;
  }
  bool read(const std::string& path, std::string* out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++reads;
    auto it = files.find(path);
    if (it == files.end()) return false;
    *out = it->second.data;
    return true;
  }
  bool write(const std::string& path, const std::string& data, bool private_file) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fail_writes) return false;
    File& f = files[path];
    f.data = data;
    f.mtime = ++clock_;
    f.private_file = private_file;
    return true;
  }
  bool remove(const std::string& path) override {
    std::lock_guard<std::mutex> lock(mutex_);
    files.erase(path);
    return true;
  }
  bool touch(const std::string& path) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files.find(path);
    if (it == files.end()) return false;
    it->second.mtime = ++clock_;
    return true;
  }
  bool list(const std::string& dir, std::vector<util::StoredFile>* out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fail_dirs) return false;
    out->clear();
    const std::string prefix = dir + "/";
    for (const auto& item : files) {
      if (item.first.compare(0, prefix.size(), prefix) == 0 &&
          item.first.find('/', prefix.size()) == std::string::npos) {
        util::StoredFile f;
        f.name = item.first.substr(prefix.size());
        f.size = item.second.data.size();
        f.mtime_s = item.second.mtime;
        out->push_back(f);
      }
    }
    return true;
  }

  std::map<std::string, File> files;
  bool fail_dirs = false;
  bool fail_writes = false;
  int reads = 0;

 private:
  std::mutex mutex_;
  std::vector<std::string> dirs_;
  int64_t clock_ = 0;
};

#endif  // PELAGIA_TESTS_MEMORY_FILE_STORE_H
