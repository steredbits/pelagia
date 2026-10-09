// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Application identity: Jellyfin trademark rules (the project name
// does not contain "Jellyfin"; compatibility is a simple mention) and
// "unofficial" mentions.

#include <string>

#include "test_framework.h"
#include "util/app_info.h"
#include "util/i18n.h"

static bool contains(const std::string& s, const char* part) {
  return s.find(part) != std::string::npos;
}

int main() {
  CHECK(!contains(util::kAppName, "Jellyfin"));
  CHECK(!contains(util::kDeviceName, "Jellyfin"));
  CHECK(contains(util::kDeviceName, util::kAppName));
  CHECK(contains(util::kDeviceName, "PS5"));
  // Tagline: Jellyfin compatibility, "unofficial" mention, platform; trademark and
  // non-affiliation mention (Jellyfin and Sony); in each interface language.
  struct Expected {
    util::Lang lang;
    const char* unofficial;
    const char* trademark;
    const char* not_affiliated;
  };
  const Expected kExpected[] = {
      {util::Lang::En, "unofficial", "trademark of the Jellyfin project", "not affiliated"},
      {util::Lang::Fr, "non officiel", "marque du projet Jellyfin", "pas affilié"}};
  for (const Expected& e : kExpected) {
    const std::string tagline = util::tr(util::Str::AboutTagline, e.lang);
    const std::string trademark = util::tr(util::Str::AboutTrademark, e.lang);
    CHECK(contains(tagline, "Jellyfin"));
    CHECK(contains(tagline, e.unofficial));
    CHECK(contains(tagline, "PS5"));
    CHECK(contains(trademark, e.trademark));
    CHECK(contains(trademark, e.not_affiliated));
    CHECK(contains(trademark, "Sony"));
  }
  return testfw::test_failures();
}
