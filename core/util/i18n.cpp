// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/i18n.h"

#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace util {

namespace {

const char* const kEnglish[] = {
#define STR(id, text) text,
#include "util/i18n/strings_en.def"
#undef STR
};

const char* const kFrench[] = {
#define STR(id, text) text,
#include "util/i18n/strings_fr.def"
#undef STR
};

constexpr size_t kCount = static_cast<size_t>(Str::Count);
static_assert(std::size(kEnglish) == kCount, "strings_en.def and the Str enum differ");
static_assert(std::size(kFrench) == kCount, "strings_fr.def lacks (or adds) strings");

std::atomic<uint8_t> g_language{static_cast<uint8_t>(Lang::En)};

std::string lowercase_base(const std::string& locale) {
  std::string out;
  for (char c : locale) {
    if (c == '_' || c == '-' || c == '.' || c == '@') break;
    out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
  }
  return out;
}

}  // namespace

Lang current_language() { return static_cast<Lang>(g_language.load()); }

void set_current_language(Lang lang) { g_language.store(static_cast<uint8_t>(lang)); }

const char* language_code(Lang lang) { return lang == Lang::Fr ? "fr" : "en"; }

const char* tr(Str id, Lang lang) {
  const size_t i = static_cast<size_t>(id);
  if (i >= kCount) return "";
  return lang == Lang::Fr ? kFrench[i] : kEnglish[i];
}

const char* tr(Str id) { return tr(id, current_language()); }

std::string trf(Str id, ...) {
  const char* format = tr(id);
  va_list args;
  va_start(args, id);
  va_list copy;
  va_copy(copy, args);
  const int n = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  std::string out;
  if (n > 0) {
    out.resize(static_cast<size_t>(n) + 1);
    std::vsnprintf(&out[0], out.size(), format, args);
    out.resize(static_cast<size_t>(n));
  }
  va_end(args);
  return out;
}

Lang language_from_locales(const std::vector<std::string>& locales, Lang fallback) {
  for (const std::string& locale : locales) {
    const std::string base = lowercase_base(locale);
    if (base == "fr" || base == "fra" || base == "fre") return Lang::Fr;
    if (base == "en" || base == "eng") return Lang::En;
  }
  return fallback;
}

const char* language_setting_code(LanguageSetting setting) {
  switch (setting) {
    case LanguageSetting::English: return "en";
    case LanguageSetting::French: return "fr";
    case LanguageSetting::Auto: break;
  }
  return "auto";
}

bool parse_language_setting(const std::string& code, LanguageSetting* out) {
  if (code == "auto") *out = LanguageSetting::Auto;
  else if (code == "en") *out = LanguageSetting::English;
  else if (code == "fr") *out = LanguageSetting::French;
  else return false;
  return true;
}

Lang resolve_language(LanguageSetting setting, const std::vector<std::string>& system_locales) {
  switch (setting) {
    case LanguageSetting::English: return Lang::En;
    case LanguageSetting::French: return Lang::Fr;
    case LanguageSetting::Auto: break;
  }
  return language_from_locales(system_locales, Lang::En);
}

}  // namespace util
