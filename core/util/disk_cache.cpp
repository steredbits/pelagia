// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "util/disk_cache.h"

#include <algorithm>
#include <vector>

#include "util/log.h"

namespace util {

namespace {

const char kSuffix[] = ".img";

bool has_suffix(const std::string& name) {
  const size_t n = sizeof(kSuffix) - 1;
  return name.size() > n && name.compare(name.size() - n, n, kSuffix) == 0;
}

}  // namespace

DiskCache::DiskCache(FileStore* store, std::string dir, uint64_t budget_bytes)
    : store_(store), dir_(std::move(dir)), budget_(budget_bytes) {}

std::string DiskCache::file_name_for(const std::string& key) {
  std::string name;
  name.reserve(key.size() + sizeof(kSuffix));
  for (char c : key) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
    name.push_back(safe ? c : '_');
  }
  return name + kSuffix;
}

std::string DiskCache::path_for(const std::string& file_name) const {
  return dir_ + "/" + file_name;
}

bool DiskCache::init() {
  std::lock_guard<std::mutex> lock(mutex_);
  ready_ = false;
  entries_.clear();
  total_ = 0;
  std::vector<StoredFile> files;
  if (!store_->make_dirs(dir_) || !store_->list(dir_, &files)) {
    LOG_WARN("Poster cache unavailable (%s): posters not kept", dir_.c_str());
    return false;
  }
  // Initial LRU order: last-use date (touched on each read).
  std::sort(files.begin(), files.end(), [](const StoredFile& a, const StoredFile& b) {
    return a.mtime_s != b.mtime_s ? a.mtime_s < b.mtime_s : a.name < b.name;
  });
  for (const StoredFile& f : files) {
    if (!has_suffix(f.name)) {
      continue;  // temporary file of an interrupted write, etc.
    }
    Entry e;
    e.size = f.size;
    e.order = next_order_++;
    entries_[f.name] = e;
    total_ += f.size;
  }
  ready_ = true;
  evict_locked();
  LOG_INFO("Poster cache: %zu file(s), %llu KB", entries_.size(),
           static_cast<unsigned long long>(total_ / 1024));
  return true;
}

bool DiskCache::get(const std::string& key, std::string* out) {
  const std::string name = file_name_for(key);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || entries_.find(name) == entries_.end()) {
      return false;
    }
  }
  const bool ok = store_->read(path_for(name), out) && !out->empty();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(name);
  if (!ok) {
    // File gone or unreadable: it is forgotten, it will be downloaded again.
    if (it != entries_.end()) {
      total_ -= it->second.size;
      entries_.erase(it);
    }
    store_->remove(path_for(name));
    return false;
  }
  if (it != entries_.end()) {
    it->second.order = next_order_++;
  }
  store_->touch(path_for(name));
  return true;
}

void DiskCache::put(const std::string& key, const std::string& data) {
  if (data.empty() || data.size() > budget_) {
    return;
  }
  const std::string name = file_name_for(key);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) {
      return;
    }
  }
  if (!store_->write(path_for(name), data, false)) {
    LOG_DEBUG("Poster cache: cannot write (%s)", name.c_str());
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Entry& e = entries_[name];
  total_ -= e.size;
  e.size = data.size();
  e.order = next_order_++;
  total_ += e.size;
  evict_locked();
}

void DiskCache::remove(const std::string& key) {
  const std::string name = file_name_for(key);
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(name);
  if (it != entries_.end()) {
    total_ -= it->second.size;
    entries_.erase(it);
  }
  store_->remove(path_for(name));
}

void DiskCache::evict_locked() {
  while (total_ > budget_ && !entries_.empty()) {
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.order < oldest->second.order) {
        oldest = it;
      }
    }
    store_->remove(path_for(oldest->first));
    total_ -= oldest->second.size;
    entries_.erase(oldest);
  }
}

uint64_t DiskCache::total_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_;
}

size_t DiskCache::entry_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

}  // namespace util
