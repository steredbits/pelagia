// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of file access on the Linux platform: folders, atomic
// write, 0600 mode of the private file (token), listing, deletion.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "platform.h"
#include "test_framework.h"

int main() {
  char tmpl[] = "/tmp/pelagia-files-XXXXXX";
  const char* base = mkdtemp(tmpl);
  CHECK(base != nullptr);
  if (!base) {
    return testfw::test_failures();
  }
  const std::string dir = std::string(base) + "/a/b/c";
  CHECK(platform::make_dirs(dir));
  CHECK(platform::make_dirs(dir));  // idempotent

  // Private file: 0600 even with a permissive umask.
  const mode_t old_mask = umask(0);
  const std::string secret = dir + "/session.json";
  CHECK(platform::file_write(secret, "{\"token\":\"x\"}", true));
  umask(old_mask);
  struct stat st;
  CHECK(stat(secret.c_str(), &st) == 0);
  CHECK((st.st_mode & 0777) == 0600);

  std::string content;
  CHECK(platform::file_read(secret, &content));
  CHECK(content == "{\"token\":\"x\"}");
  CHECK(platform::file_write(secret, "v2", true));  // remplacement atomique
  CHECK(platform::file_read(secret, &content) && content == "v2");

  CHECK(platform::file_write(dir + "/poster.img", std::string(1000, 'p'), false));
  std::vector<platform::FileInfo> files;
  CHECK(platform::list_dir(dir, &files));
  CHECK_EQ(files.size(), 2u);  // no temporary file left behind
  for (const platform::FileInfo& f : files) {
    CHECK(f.name == "session.json" || (f.name == "poster.img" && f.size == 1000));
  }
  CHECK(platform::file_touch(dir + "/poster.img"));
  CHECK(!platform::file_touch(dir + "/absent"));

  CHECK(platform::file_remove(secret));
  CHECK(platform::file_remove(secret));  // already absent: not an error
  CHECK(!platform::file_read(secret, &content));
  CHECK(!platform::list_dir(dir + "/absent", &files));

  const std::string cleanup = std::string("rm -rf ") + base;
  CHECK(std::system(cleanup.c_str()) == 0);
  return testfw::test_failures();
}
