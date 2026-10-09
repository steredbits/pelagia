// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "util/languages.h"

#include "util/i18n.h"


namespace util {

namespace {

struct LanguageInfo {
    const char* iso1;  // ISO 639-1
    const char* iso2b;  // ISO 639-2/B (forme canonique)
    const char* iso2t;  // ISO 639-2/T (same value if no variant)
    const char* name_en;  // English name
    const char* name_fr;  // French name
};

const LanguageInfo kLanguages[] = {
    {"fr", "fre", "fra", "French", "Français"},
    {"en", "eng", "eng", "English", "Anglais"},
    {"de", "ger", "deu", "German", "Allemand"},
    {"es", "spa", "spa", "Spanish", "Espagnol"},
    {"it", "ita", "ita", "Italian", "Italien"},
    {"pt", "por", "por", "Portuguese", "Portugais"},
    {"nl", "dut", "nld", "Dutch", "Néerlandais"},
    {"ja", "jpn", "jpn", "Japanese", "Japonais"},
    {"ko", "kor", "kor", "Korean", "Coréen"},
    {"zh", "chi", "zho", "Chinese", "Chinois"},
    {"", "yue", "yue", "Cantonese", "Cantonais"},
    {"ru", "rus", "rus", "Russian", "Russe"},
    {"pl", "pol", "pol", "Polish", "Polonais"},
    {"sv", "swe", "swe", "Swedish", "Suédois"},
    {"no", "nor", "nor", "Norwegian", "Norvégien"},
    {"nb", "nob", "nob", "Norwegian Bokmål", "Norvégien bokmål"},
    {"da", "dan", "dan", "Danish", "Danois"},
    {"fi", "fin", "fin", "Finnish", "Finnois"},
    {"is", "ice", "isl", "Icelandic", "Islandais"},
    {"tr", "tur", "tur", "Turkish", "Turc"},
    {"ar", "ara", "ara", "Arabic", "Arabe"},
    {"he", "heb", "heb", "Hebrew", "Hébreu"},
    {"hi", "hin", "hin", "Hindi", "Hindi"},
    {"el", "gre", "ell", "Greek", "Grec"},
    {"cs", "cze", "ces", "Czech", "Tchèque"},
    {"sk", "slo", "slk", "Slovak", "Slovaque"},
    {"hu", "hun", "hun", "Hungarian", "Hongrois"},
    {"ro", "rum", "ron", "Romanian", "Roumain"},
    {"bg", "bul", "bul", "Bulgarian", "Bulgare"},
    {"uk", "ukr", "ukr", "Ukrainian", "Ukrainien"},
    {"hr", "hrv", "hrv", "Croatian", "Croate"},
    {"sr", "srp", "srp", "Serbian", "Serbe"},
    {"sl", "slv", "slv", "Slovenian", "Slovène"},
    {"th", "tha", "tha", "Thai", "Thaï"},
    {"vi", "vie", "vie", "Vietnamese", "Vietnamien"},
    {"id", "ind", "ind", "Indonesian", "Indonésien"},
    {"ms", "may", "msa", "Malay", "Malais"},
    {"fa", "per", "fas", "Persian", "Persan"},
    {"ca", "cat", "cat", "Catalan", "Catalan"},
    {"eu", "baq", "eus", "Basque", "Basque"},
    {"gl", "glg", "glg", "Galician", "Galicien"},
    {"la", "lat", "lat", "Latin", "Latin"},
    {"lt", "lit", "lit", "Lithuanian", "Lituanien"},
    {"lv", "lav", "lav", "Latvian", "Letton"},
    {"et", "est", "est", "Estonian", "Estonien"},
};

bool equals_ignore_case(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

// Lowercase code, without region suffix ("fr-FR" / "fr_CA" -> "fr").
std::string base_code(const std::string& code) {
    std::string out;
    for (char c : code) {
        if (c == '-' || c == '_') break;
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
    }
    return out;
}

const LanguageInfo* find_language(const std::string& code) {
    const std::string base = base_code(code);
    if (base.empty()) return nullptr;
    for (const LanguageInfo& info : kLanguages) {
        if ((info.iso1[0] != '\0' && base == info.iso1) || base == info.iso2b ||
            base == info.iso2t) {
            return &info;
        }
    }
    return nullptr;
}

}  // namespace

bool language_undefined(const std::string& code) {
    static const char* const kUndefined[] = {"", "und", "unknown", "undetermined", "mul", "zxx"};
    for (const char* u : kUndefined) {
        if (equals_ignore_case(code.c_str(), u)) return true;
    }
    return false;
}

std::string canonical_language(const std::string& code) {
    const LanguageInfo* info = find_language(code);
    return info ? std::string(info->iso2b) : base_code(code);
}

bool same_language(const std::string& a, const std::string& b) {
    const std::string ca = canonical_language(a);
    return !ca.empty() && ca == canonical_language(b);
}

std::string language_name(const std::string& code, Lang lang) {
    if (language_undefined(code)) return tr(Str::LanguageUnknown, lang);
    if (const LanguageInfo* info = find_language(code)) {
        return lang == Lang::Fr ? info->name_fr : info->name_en;
    }
    std::string out = base_code(code);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return out;
}

std::string language_name(const std::string& code) {
    return language_name(code, current_language());
}

}  // namespace util
