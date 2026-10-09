// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// PS5 locations (platform/ps5/ps5_paths.h, pure functions): launched from
// a USB stick or from /data, the application only writes under
// /data/homebrew/Pelagia and reads pelagia.conf on the stick then in /data.

#include <string>
#include <vector>

#include "memory_file_store.h"
#include "ps5_paths.h"
#include "test_framework.h"
#include "util/app_info.h"
#include "util/preconfig.h"

static bool starts_with(const std::string& s, const std::string& prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

int main() {
  // The folder has the application's name (USB stick and /data).
  CHECK_EQ(std::string(ps5_paths::kAppDir), std::string(util::kAppName));
  CHECK_EQ(std::string(ps5_paths::kDataRoot), "/data/homebrew/" + std::string(util::kAppName));

  // Everything that is written is under /data/homebrew/Pelagia, never on /mnt.
  const std::string root = ps5_paths::kDataRoot;
  const std::vector<std::string> written = {ps5_paths::config_dir(), ps5_paths::cache_dir(),
                                            ps5_paths::log_dir()};
  for (const std::string& dir : written) {
    CHECK(starts_with(dir, root + "/"));
    CHECK(!starts_with(dir, "/mnt"));
  }

  // pelagia.conf: USB sticks 0..7 in order, then /data last.
  const std::vector<std::string> paths = ps5_paths::preconfig_files();
  CHECK_EQ(paths.size(), 9u);
  CHECK_EQ(paths.front(), "/mnt/usb0/homebrew/Pelagia/pelagia.conf");
  CHECK_EQ(paths[7], "/mnt/usb7/homebrew/Pelagia/pelagia.conf");
  CHECK_EQ(paths.back(), "/data/homebrew/Pelagia/pelagia.conf");
  CHECK_EQ(util::usb_preconfig_path(3, "Pelagia"), "/mnt/usb3/homebrew/Pelagia/pelagia.conf");

  // The stick comes before /data; a file without server= or a missing one moves on to the next.
  {
    MemoryFileStore files;
    files.write("/data/homebrew/Pelagia/pelagia.conf", "server=http://data:8096\n", false);
    std::string source;
    CHECK_EQ(util::find_preconfig_server(paths, &files, &source), "http://data:8096");
    CHECK_EQ(source, "/data/homebrew/Pelagia/pelagia.conf");

    files.write("/mnt/usb1/homebrew/Pelagia/pelagia.conf", "# vide\n", false);
    CHECK_EQ(util::find_preconfig_server(paths, &files, &source), "http://data:8096");

    files.write("/mnt/usb2/homebrew/Pelagia/pelagia.conf", "server=http://usb:8096\n", false);
    CHECK_EQ(util::find_preconfig_server(paths, &files, &source), "http://usb:8096");
    CHECK_EQ(source, "/mnt/usb2/homebrew/Pelagia/pelagia.conf");

    // Read only: no file created or modified by the search.
    CHECK_EQ(files.files.size(), 3u);
  }
  {
    MemoryFileStore none;
    std::string source = "unchanged";
    CHECK_EQ(util::find_preconfig_server(paths, &none, &source), "");
    CHECK_EQ(source, "unchanged");
  }
  return testfw::test_failures();
}
