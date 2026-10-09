// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_I18N_H
#define PELAGIA_CORE_UTIL_I18N_H

// User interface localization (English, French). The strings live in
// util/i18n/strings_<lang>.def, one STR(Identifier, "text") per line, in the same
// order in both files. English is the default language and the fallback.
// Texts with variable parts are printf-style formats (trf), so that each
// language can order the words as it needs.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace util {

enum class Lang : uint8_t { En, Fr };

enum class Str : uint16_t {
#define STR(id, text) id,
#include "util/i18n/strings_en.def"
#undef STR
  Count
};

// Language used by tr() / trf(); English until changed. Thread-safe.
Lang current_language();
void set_current_language(Lang lang);

const char* language_code(Lang lang);  // "en", "fr"

// The string in the current language (or in `lang`). Never null.
const char* tr(Str id);
const char* tr(Str id, Lang lang);
// printf-style formatting of a string of the catalog.
std::string trf(Str id, ...);

// Language of the first locale of the list ("fr_FR", "fr-CA", "en_US.UTF-8"...)
// that the application translates; `fallback` if there is none.
Lang language_from_locales(const std::vector<std::string>& locales, Lang fallback = Lang::En);

// Language choice made by the user (Options menu), saved between launches.
enum class LanguageSetting : uint8_t { Auto, English, French };

const char* language_setting_code(LanguageSetting setting);  // "auto", "en", "fr"
bool parse_language_setting(const std::string& code, LanguageSetting* out);
// Auto: language of the system locales (English if unknown).
Lang resolve_language(LanguageSetting setting, const std::vector<std::string>& system_locales);

// Changes the current language for the lifetime of the object (tests).
class ScopedLanguage {
 public:
  explicit ScopedLanguage(Lang lang) : previous_(current_language()) { set_current_language(lang); }
  ~ScopedLanguage() { set_current_language(previous_); }
  ScopedLanguage(const ScopedLanguage&) = delete;
  ScopedLanguage& operator=(const ScopedLanguage&) = delete;

 private:
  Lang previous_;
};

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_I18N_H
