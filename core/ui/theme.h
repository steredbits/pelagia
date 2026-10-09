// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_THEME_H
#define PELAGIA_CORE_UI_THEME_H

// Single dark theme, designed for a TV seen from afar ("10-foot UI") at
// 1920x1080: 5% safe margins (TV overscan), text of
// 24 px at least, very visible focus.

#include "ui/canvas.h"

namespace ui {
namespace theme {

constexpr int kScreenW = 1920;
constexpr int kScreenH = 1080;
constexpr int kMarginX = 96;  // 5% of 1920
constexpr int kMarginY = 54;  // 5% of 1080
constexpr int kContentW = kScreenW - 2 * kMarginX;

// Text sizes (px per em).
constexpr int kTitle = 56;
constexpr int kHeading = 36;
constexpr int kBody = 28;
constexpr int kSmall = 24;

// Affiches (2:3).
constexpr int kPosterW = 226;  // grille : 7 colonnes
constexpr int kPosterH = 339;
constexpr int kRowPosterW = 200;  // home rows
constexpr int kRowPosterH = 300;
constexpr int kGap = 24;
constexpr int kFocusBorder = 5;
// Width requested from the server (and decoded) for list and
// page posters: a single size in lists to share the cache.
constexpr int kPosterFetchW = 240;
constexpr int kDetailPosterW = 400;

inline Color background() { return rgba(18, 20, 28); }
inline Color surface() { return rgba(32, 36, 50); }
inline Color surface_focus() { return rgba(48, 54, 76); }
inline Color accent() { return rgba(0, 164, 220); }  // bleu Jellyfin
inline Color text() { return rgba(236, 238, 244); }
inline Color text_dim() { return rgba(160, 166, 184); }
inline Color danger() { return rgba(238, 96, 96); }
inline Color focus_ring() { return rgba(255, 255, 255); }
inline Color scrim() { return rgba(0, 0, 0, 170); }

}  // namespace theme
}  // namespace ui

#endif  // PELAGIA_CORE_UI_THEME_H
