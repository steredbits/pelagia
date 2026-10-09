// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_ABOUT_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_ABOUT_SCREEN_H

// "About": name, tagline, license and legal notices (unofficial
// project, Jellyfin trademark). No loading: a single static screen.

#include "ui/screen.h"

namespace ui {

class AboutScreen final : public Screen {
 public:
  const char* name() const override { return "about"; }
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_ABOUT_SCREEN_H
