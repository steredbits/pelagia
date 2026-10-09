// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_FILE_STORE_H
#define PELAGIA_CORE_UTIL_FILE_STORE_H

// File access as seen by the core (configuration, poster cache). The application
// passes an implementation on top of platform.h (ui/platform_bridge); the
// tests pass an in-memory store. Implementations must be
// usable from several threads.

#include <cstdint>
#include <string>
#include <vector>

namespace util {

struct StoredFile {
  std::string name;
  uint64_t size = 0;
  int64_t mtime_s = 0;
};

class FileStore {
 public:
  virtual ~FileStore() {}
  virtual bool make_dirs(const std::string& path) = 0;
  virtual bool read(const std::string& path, std::string* out) = 0;
  // Atomic; private_file: readable by the owner only (token).
  virtual bool write(const std::string& path, const std::string& data,
                     bool private_file) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool touch(const std::string& path) = 0;
  virtual bool list(const std::string& dir, std::vector<StoredFile>* out) = 0;
};

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_FILE_STORE_H
