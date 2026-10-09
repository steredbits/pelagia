// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Localization: catalogs, formats, language selection.

#include <cstring>
#include <string>
#include <vector>

#include "test_framework.h"
#include "util/i18n.h"

namespace {

// Identifiers in the order of each catalog file: the two files must list the
// same identifiers in the same order.
const char* const kEnglishIds[] = {
#define STR(id, text) #id,
#include "util/i18n/strings_en.def"
#undef STR
};
const char* const kFrenchIds[] = {
#define STR(id, text) #id,
#include "util/i18n/strings_fr.def"
#undef STR
};

// Conversion specifications of a format ("%s %d" -> "sd"), "%%" ignored.
std::string conversions(const char* format) {
  std::string out;
  for (const char* p = format; *p; ++p) {
    if (*p != '%') continue;
    ++p;
    if (*p == '%') continue;
    while (*p && std::strchr("-+ #0123456789.lhz", *p)) ++p;
    if (!*p) break;
    out.push_back(*p);
  }
  return out;
}

bool has_non_ascii(const char* s) {
  for (; *s; ++s) {
    if (static_cast<unsigned char>(*s) >= 0x80) return true;
  }
  return false;
}

void check_catalogs() {
  const size_t n = static_cast<size_t>(util::Str::Count);
  CHECK(n > 50);
  CHECK_EQ(sizeof(kEnglishIds) / sizeof(kEnglishIds[0]), n);
  CHECK_EQ(sizeof(kFrenchIds) / sizeof(kFrenchIds[0]), n);
  for (size_t i = 0; i < n; ++i) {
    const util::Str id = static_cast<util::Str>(i);
    CHECK_EQ(std::string(kEnglishIds[i]), std::string(kFrenchIds[i]));
    const char* en = util::tr(id, util::Lang::En);
    const char* fr = util::tr(id, util::Lang::Fr);
    CHECK(en[0] != '\0');
    CHECK(fr[0] != '\0');
    // Same placeholders in both languages (same types, same order).
    CHECK_EQ(conversions(en), conversions(fr));
  }
}

void check_selection() {
  using util::Lang;
  using util::LanguageSetting;
  CHECK(util::language_from_locales({}) == Lang::En);
  CHECK(util::language_from_locales({"fr_FR"}) == Lang::Fr);
  CHECK(util::language_from_locales({"fr-CA"}) == Lang::Fr);
  CHECK(util::language_from_locales({"FR"}) == Lang::Fr);
  CHECK(util::language_from_locales({"fr_FR.UTF-8"}) == Lang::Fr);
  CHECK(util::language_from_locales({"en_US"}) == Lang::En);
  // First translated language of the preference list.
  CHECK(util::language_from_locales({"de_DE", "fr_FR", "en_US"}) == Lang::Fr);
  CHECK(util::language_from_locales({"de_DE", "en_US", "fr_FR"}) == Lang::En);
  // Unknown languages: English (or the requested fallback).
  CHECK(util::language_from_locales({"ja_JP"}) == Lang::En);
  CHECK(util::language_from_locales({"ja_JP"}, Lang::Fr) == Lang::Fr);

  CHECK(util::resolve_language(LanguageSetting::Auto, {"fr_FR"}) == Lang::Fr);
  CHECK(util::resolve_language(LanguageSetting::Auto, {}) == Lang::En);
  CHECK(util::resolve_language(LanguageSetting::English, {"fr_FR"}) == Lang::En);
  CHECK(util::resolve_language(LanguageSetting::French, {"en_US"}) == Lang::Fr);

  LanguageSetting setting = LanguageSetting::French;
  CHECK(util::parse_language_setting("auto", &setting) && setting == LanguageSetting::Auto);
  CHECK(util::parse_language_setting("en", &setting) && setting == LanguageSetting::English);
  CHECK(util::parse_language_setting("fr", &setting) && setting == LanguageSetting::French);
  setting = LanguageSetting::English;
  CHECK(!util::parse_language_setting("de", &setting));
  CHECK(!util::parse_language_setting("", &setting));
  CHECK(setting == LanguageSetting::English);  // unchanged on failure
  CHECK_EQ(std::string(util::language_setting_code(LanguageSetting::Auto)), "auto");
  CHECK_EQ(std::string(util::language_setting_code(LanguageSetting::English)), "en");
  CHECK_EQ(std::string(util::language_setting_code(LanguageSetting::French)), "fr");
}

void check_current_language() {
  CHECK(util::current_language() == util::Lang::En);  // default
  CHECK_EQ(std::string(util::tr(util::Str::Retry)), "Retry");
  {
    util::ScopedLanguage fr(util::Lang::Fr);
    CHECK(util::current_language() == util::Lang::Fr);
    CHECK_EQ(std::string(util::tr(util::Str::Retry)), "Réessayer");
    CHECK_EQ(util::trf(util::Str::ResumeAt, "20:00"), "Reprendre à 20:00");
  }
  CHECK(util::current_language() == util::Lang::En);
  CHECK_EQ(util::trf(util::Str::ResumeAt, "20:00"), "Resume at 20:00");
  CHECK_EQ(util::trf(util::Str::ErrServerError, 503), "Server error (HTTP 503)");
  // Interface texts translated in French carry accents; the English ones do not
  // (apart from the native names of the languages and the punctuation).
  CHECK(has_non_ascii(util::tr(util::Str::Loading, util::Lang::Fr)));
  CHECK(!has_non_ascii(util::tr(util::Str::SignOut, util::Lang::En)));
  CHECK_EQ(std::string(util::language_code(util::Lang::En)), "en");
  CHECK_EQ(std::string(util::language_code(util::Lang::Fr)), "fr");
}

}  // namespace

int main() {
  check_catalogs();
  check_selection();
  check_current_language();
  return testfw::test_failures();
}
