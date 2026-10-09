// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Files (POSIX), shared by Linux and the PS5 (FreeBSD libc). The application
// folders (config_dir, cache_dir) are specific to each platform.

#include "platform.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace platform {

namespace {

bool write_all(int fd, const std::string& data) {
  size_t done = 0;
  while (done < data.size()) {
    const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    done += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

bool make_dirs(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  for (size_t pos = 1; pos <= path.size(); ++pos) {
    if (pos == path.size() || path[pos] == '/') {
      const std::string part = path.substr(0, pos);
      if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
      }
    }
  }
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_read(const std::string& path, std::string* out) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  out->clear();
  char buf[65536];
  bool ok = true;
  for (;;) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ok = false;
      break;
    }
    out->append(buf, static_cast<size_t>(n));
  }
  ::close(fd);
  return ok;
}

bool file_write(const std::string& path, const std::string& data, bool private_file) {
  // Unique temporary file in the same folder: the rename is atomic,
  // a reader never sees a half-written file.
  static int counter = 0;
  char suffix[64];
  std::snprintf(suffix, sizeof(suffix), ".tmp%d-%d", static_cast<int>(::getpid()),
                __atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED));
  const std::string tmp = path + suffix;
  const mode_t mode = private_file ? 0600 : 0644;
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (fd < 0) {
    return false;
  }
  // open() applies the umask: the exact mode is forced (0600 for the token).
  bool ok = ::fchmod(fd, mode) == 0 && write_all(fd, data);
  ok = (::close(fd) == 0) && ok;
  if (ok && ::rename(tmp.c_str(), path.c_str()) == 0) {
    return true;
  }
  ::unlink(tmp.c_str());
  return false;
}

bool file_remove(const std::string& path) {
  return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}

bool file_touch(const std::string& path) {
  return ::utimes(path.c_str(), nullptr) == 0;
}

bool list_dir(const std::string& path, std::vector<FileInfo>* out) {
  out->clear();
  DIR* dir = ::opendir(path.c_str());
  if (!dir) {
    return false;
  }
  while (struct dirent* entry = ::readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    struct stat st;
    if (::stat((path + "/" + name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }
    FileInfo info;
    info.name = name;
    info.size = static_cast<uint64_t>(st.st_size);
    info.mtime_s = static_cast<int64_t>(st.st_mtime);
    out->push_back(info);
  }
  ::closedir(dir);
  return true;
}

}  // namespace platform
