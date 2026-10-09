// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_EMBEDDED_FONTS_H
#define PELAGIA_CORE_UI_EMBEDDED_FONTS_H

// Fonts compiled into the binary (assets/fonts, see its README): arrays
// generated at build time by cmake/embed_fonts.cmake.

#include <cstddef>

namespace ui {

extern const unsigned char kNotoSansRegular[];
extern const size_t kNotoSansRegularSize;
extern const unsigned char kNotoSansBold[];
extern const size_t kNotoSansBoldSize;

}  // namespace ui

#endif  // PELAGIA_CORE_UI_EMBEDDED_FONTS_H
