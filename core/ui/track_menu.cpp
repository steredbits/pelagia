// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/track_menu.h"

#include "api/track_labels.h"
#include "util/i18n.h"

namespace ui {

namespace {

std::string choice_label(const api::MediaStreamInfo& s) {
  std::string label = s.kind == api::StreamKind::Subtitle ? api::subtitle_label(s)
                                                          : api::audio_label(s);
  if (s.kind == api::StreamKind::Subtitle &&
      api::subtitle_delivery_for(s) == api::SubtitleDelivery::Encode) {
    label += std::string(" · ") + util::tr(util::Str::TrackBurnedIn);
  }
  return label;
}

}  // namespace

bool has_track_choices(const api::MediaSourceInfo& source) {
  for (const api::MediaStreamInfo& s : source.streams) {
    if (s.kind == api::StreamKind::Audio || s.kind == api::StreamKind::Subtitle) return true;
  }
  return false;
}

std::vector<TrackChoice> audio_choices(const api::MediaSourceInfo& source,
                                       const api::TrackSelection& tracks) {
  std::vector<TrackChoice> out;
  for (const api::MediaStreamInfo* s : api::streams_of(source, api::StreamKind::Audio)) {
    out.push_back(TrackChoice{choice_label(*s), s->index, s->index == tracks.audio_index});
  }
  return out;
}

std::vector<TrackChoice> subtitle_choices(const api::MediaSourceInfo& source,
                                          const api::TrackSelection& tracks) {
  std::vector<TrackChoice> out;
  out.push_back(TrackChoice{util::tr(util::Str::None), -1, tracks.subtitle_index < 0});
  for (const api::MediaStreamInfo* s : api::streams_of(source, api::StreamKind::Subtitle)) {
    out.push_back(TrackChoice{choice_label(*s), s->index, s->index == tracks.subtitle_index});
  }
  return out;
}

std::string audio_summary(const api::MediaSourceInfo& source, const api::TrackSelection& tracks) {
  const api::MediaStreamInfo* s = api::find_stream(source, tracks.audio_index);
  return s ? api::audio_label(*s) : std::string(util::tr(util::Str::Automatic));
}

std::string subtitle_summary(const api::MediaSourceInfo& source,
                             const api::TrackSelection& tracks) {
  const api::MediaStreamInfo* s = tracks.subtitle_index >= 0
                                      ? api::find_stream(source, tracks.subtitle_index)
                                      : nullptr;
  return s ? choice_label(*s) : std::string(util::tr(util::Str::None));
}

std::vector<MenuItem> track_menu_items(const std::vector<TrackChoice>& choices,
                                       std::function<void(App&, int)> on_pick, int* initial) {
  std::vector<MenuItem> items;
  *initial = 0;
  for (size_t i = 0; i < choices.size(); ++i) {
    const int index = choices[i].index;
    MenuItem item;
    item.label = choices[i].label;
    item.checked = choices[i].selected;
    item.action = [on_pick, index](App& app) { on_pick(app, index); };
    if (choices[i].selected) *initial = static_cast<int>(i);
    items.push_back(item);
  }
  return items;
}

}  // namespace ui
