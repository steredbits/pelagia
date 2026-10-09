// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/text.h"

namespace util {

namespace {

bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// Base letter of the characters U+00C0-U+00FF then U+0100-U+017F, 16 per
// line. '*': two-letter equivalent (see multi_letter_fold);
// '#': not a letter (x and /), kept as is.
const char kLatin1Fold[] =
    "aaaaaa*ceeeeiiii"   // U+00C0
    "dnooooo#ouuuuy**"   // U+00D0
    "aaaaaa*ceeeeiiii"   // U+00E0
    "dnooooo#ouuuuy*y";  // U+00F0
const char kLatinExtAFold[] =
    "aaaaaaccccccccdd"   // U+0100
    "ddeeeeeeeeeegggg"   // U+0110
    "gggghhhhiiiiiiii"   // U+0120
    "ii**jjkkklllllll"   // U+0130
    "lllnnnnnnnnnoooo"   // U+0140
    "oo**rrrrrrssssss"   // U+0150
    "ssttttttuuuuuuuu"   // U+0160
    "uuuuwwyyyzzzzzzs";  // U+0170

const char* multi_letter_fold(uint32_t cp) {
    switch (cp) {
        case 0xC6: case 0xE6: return "ae";
        case 0xDE: case 0xFE: return "th";
        case 0xDF: return "ss";
        case 0x132: case 0x133: return "ij";
        case 0x152: case 0x153: return "oe";
        default: return nullptr;
    }
}

// Appends the folded form (lowercase without accent) of cp; false if cp has
// no known fold.
bool append_folded(std::string* out, uint32_t cp) {
    if (cp < 0x80) {
        char c = static_cast<char>(cp);
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        out->push_back(c);
        return true;
    }
    char base = 0;
    if (cp >= 0xC0 && cp <= 0xFF) {
        base = kLatin1Fold[cp - 0xC0];
    } else if (cp >= 0x100 && cp <= 0x17F) {
        base = kLatinExtAFold[cp - 0x100];
    }
    if (base == '*') {
        out->append(multi_letter_fold(cp));
        return true;
    }
    if (base != 0 && base != '#') {
        out->push_back(base);
        return true;
    }
    return false;
}

bool is_control_char(uint32_t cp) {
    return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

}  // namespace

uint32_t utf8_next(const std::string& s, size_t* pos) {
    const size_t i = *pos;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        *pos = i + 1;
        return c;
    }
    int len = 0;
    uint32_t cp = 0;
    uint32_t min = 0;
    if ((c & 0xE0) == 0xC0) {
        len = 2, cp = c & 0x1F, min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3, cp = c & 0x0F, min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4, cp = c & 0x07, min = 0x10000;
    } else {
        *pos = i + 1;
        return kReplacementChar;
    }
    if (i + len > s.size()) {
        *pos = i + 1;
        return kReplacementChar;
    }
    for (int k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if (!is_continuation(cc)) {
            *pos = i + 1;
            return kReplacementChar;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *pos = i + 1;
        return kReplacementChar;
    }
    *pos = i + len;
    return cp;
}

void utf8_append(std::string* out, uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = kReplacementChar;
    }
    if (cp < 0x80) {
        out->push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (size_t pos = 0; pos < s.size(); ++n) {
        utf8_next(s, &pos);
    }
    return n;
}

void utf8_pop_back(std::string* s) {
    if (s->empty()) {
        return;
    }
    // Goes back to the start of the last sequence (at most 3 continuation bytes).
    size_t start = s->size() - 1;
    int guard = 0;
    while (start > 0 && guard < 3 &&
           is_continuation(static_cast<unsigned char>((*s)[start]))) {
        --start;
        ++guard;
    }
    size_t pos = start;
    utf8_next(*s, &pos);
    // Complete and valid sequence: removed whole; otherwise one byte.
    s->resize(pos == s->size() ? start : s->size() - 1);
}

bool is_invisible_char(uint32_t cp) {
    return cp == 0x00AD ||                     // trait d'union conditionnel
           cp == 0x034F ||                     // combining grapheme joiner
           cp == 0x061C ||                     // arabic letter mark
           cp == 0x180E ||                     // Mongolian vowel separator
           (cp >= 0x200B && cp <= 0x200F) ||   // ZWSP, ZWNJ, ZWJ, LRM, RLM
           (cp >= 0x202A && cp <= 0x202E) ||   // bidirectional embeddings
           (cp >= 0x2060 && cp <= 0x206F) ||   // word joiner, isolats, ...
           cp == 0xFEFF ||                     // BOM / ZWNBSP
           (cp >= 0xFFF9 && cp <= 0xFFFB) ||   // interlinear annotations
           (cp >= 0xE0000 && cp <= 0xE007F);   // tags
}

bool is_space_char(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000;
}

std::string clean_display_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool pending_space = false;
    for (size_t pos = 0; pos < s.size();) {
        const uint32_t cp = utf8_next(s, &pos);
        if (is_invisible_char(cp)) {
            continue;
        }
        if (is_space_char(cp) || is_control_char(cp)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        utf8_append(&out, cp);
    }
    return out;
}

std::string sort_key(const std::string& s) {
    const std::string clean = clean_display_text(s);
    std::string out;
    out.reserve(clean.size());
    for (size_t pos = 0; pos < clean.size();) {
        const uint32_t cp = utf8_next(clean, &pos);
        if (!append_folded(&out, cp)) {
            utf8_append(&out, cp);
        }
    }
    return out;
}

}  // namespace util
