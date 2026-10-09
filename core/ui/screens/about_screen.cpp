// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/screens/about_screen.h"

#include "ui/app.h"
#include "ui/screens/screens.h"
#include "ui/theme.h"
#include "util/app_info.h"
#include "util/version.h"
#include "util/i18n.h"

namespace ui {

void AboutScreen::handle(App& app, const platform::Input& in) {
  if (in.event == platform::InputEvent::Back || in.event == platform::InputEvent::Ok) {
    app.pop();
  }
}

void AboutScreen::draw(App& app, Painter& p) {
  (void)app;
  const int x = 560;
  const int w = 800;
  p.label(util::kAppName, x, 150, Weight::Bold, theme::kTitle, theme::accent());
  p.label(util::tr(util::Str::AboutTagline), x, 230, Weight::Regular, theme::kBody,
          theme::text_dim());
  p.label(util::trf(util::Str::AboutVersion, util::kVersion), x, 290, Weight::Regular, theme::kBody,
          theme::text_dim());
  int y = 380;
  y += p.paragraph(util::tr(util::Str::AboutLicense), rect(x, y, w, 80), Weight::Regular, theme::kBody,
                   theme::text(), 2) + theme::kGap;
  y += p.paragraph(util::tr(util::Str::AboutTrademark), rect(x, y, w, 120), Weight::Regular, theme::kBody,
                   theme::text(), 4) + theme::kGap;
  p.paragraph(util::tr(util::Str::AboutOwnHardware), rect(x, y, w, 80), Weight::Regular, theme::kBody,
              theme::text(), 2);
  p.hints(util::tr(util::Str::HintBack));
}

}  // namespace ui
