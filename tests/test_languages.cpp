// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Language codes: mapping between ISO 639-1/639-2 and language names.

#include "util/languages.h"

#include "test_framework.h"

int main() {
    // Same language in all its forms.
    CHECK(util::same_language("fre", "fra"));
    CHECK(util::same_language("fr", "fre"));
    CHECK(util::same_language("FR", "fra"));
    CHECK(util::same_language("fr-FR", "fre"));
    CHECK(util::same_language("fr_CA", "fr"));
    CHECK(util::same_language("ger", "deu"));
    CHECK(util::same_language("chi", "zh-CN"));
    CHECK(!util::same_language("fre", "eng"));
    CHECK(!util::same_language("", "fre"));
    CHECK(!util::same_language("", ""));
    // Unknown code: compared as is, case-insensitive.
    CHECK(util::same_language("tlh", "TLH"));
    CHECK(!util::same_language("tlh", "xyz"));
    CHECK_EQ(util::canonical_language("fra"), "fre");
    CHECK_EQ(util::canonical_language("XYZ"), "xyz");

    // Undetermined language (server list).
    CHECK(util::language_undefined(""));
    CHECK(util::language_undefined("und"));
    CHECK(util::language_undefined("UND"));
    CHECK(util::language_undefined("mul"));
    CHECK(util::language_undefined("zxx"));
    CHECK(util::language_undefined("unknown"));
    CHECK(util::language_undefined("undetermined"));
    CHECK(!util::language_undefined("fre"));

    // Language names, in the requested language and in the interface language.
    CHECK_EQ(util::language_name("fre", util::Lang::Fr), "Français");
    CHECK_EQ(util::language_name("fra", util::Lang::Fr), "Français");
    CHECK_EQ(util::language_name("eng", util::Lang::Fr), "Anglais");
    CHECK_EQ(util::language_name("jpn", util::Lang::Fr), "Japonais");
    CHECK_EQ(util::language_name("und", util::Lang::Fr), "Langue inconnue");
    CHECK_EQ(util::language_name("", util::Lang::Fr), "Langue inconnue");
    CHECK_EQ(util::language_name("tlh", util::Lang::Fr), "TLH");
    CHECK_EQ(util::language_name("fre", util::Lang::En), "French");
    CHECK_EQ(util::language_name("fra", util::Lang::En), "French");
    CHECK_EQ(util::language_name("eng", util::Lang::En), "English");
    CHECK_EQ(util::language_name("jpn", util::Lang::En), "Japanese");
    CHECK_EQ(util::language_name("und", util::Lang::En), "Unknown language");
    CHECK_EQ(util::language_name("tlh", util::Lang::En), "TLH");
    CHECK_EQ(util::language_name("fre"), "French");  // English by default
    {
        util::ScopedLanguage fr(util::Lang::Fr);
        CHECK_EQ(util::language_name("fre"), "Français");
    }
    CHECK_EQ(util::language_name("fre"), "French");
    return testfw::test_failures();
}
