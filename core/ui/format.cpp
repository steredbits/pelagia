// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/format.h"

#include <cstdio>

#include "util/text.h"

namespace ui {

std::string format_clock(int64_t ms) {
  const int64_t total = ms > 0 ? ms / 1000 : 0;
  const long long h = static_cast<long long>(total / 3600);
  const int m = static_cast<int>(total / 60 % 60);
  const int s = static_cast<int>(total % 60);
  char buf[32];
  if (h > 0) {
    std::snprintf(buf, sizeof(buf), "%lld:%02d:%02d", h, m, s);
  } else {
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  }
  return buf;
}

std::string format_runtime(int64_t ms) {
  const int64_t minutes = ms > 0 ? (ms + 30000) / 60000 : 0;
  char buf[32];
  if (minutes < 1) {
    return "< 1 min";
  }
  if (minutes < 60) {
    std::snprintf(buf, sizeof(buf), "%lld min", static_cast<long long>(minutes));
  } else if (minutes % 60 == 0) {
    std::snprintf(buf, sizeof(buf), "%lld h", static_cast<long long>(minutes / 60));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld h %02lld", static_cast<long long>(minutes / 60),
                  static_cast<long long>(minutes % 60));
  }
  return buf;
}

std::string episode_code(const api::MediaItem& item) {
  if (item.index_number < 0 || item.parent_index_number < 0) {
    return "";
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "S%02dE%02d", item.parent_index_number, item.index_number);
  return buf;
}

std::string display_title(const api::MediaItem& item) {
  return util::clean_display_text(item.name);
}

std::string item_sort_key(const api::MediaItem& item) {
  const std::string key = util::sort_key(item.sort_name);
  return key.empty() ? util::sort_key(item.name) : key;
}

int64_t resume_position_ms(const api::MediaItem& item) {
  if (item.played || item.playback_position_ticks <= 0) {
    return 0;
  }
  return item.playback_position_ticks / kTicksPerMs;
}

double progress_fraction(const api::MediaItem& item) {
  const int64_t pos = resume_position_ms(item);
  const int64_t total = item.runtime_ticks / kTicksPerMs;
  if (pos <= 0 || total <= 0) {
    return 0.0;
  }
  return pos >= total ? 1.0 : static_cast<double>(pos) / static_cast<double>(total);
}

bool is_playable(const api::MediaItem& item) {
  return (item.type == "Movie" || item.type == "Episode" || item.type == "Video") &&
         item.runtime_ticks > 0;
}

PosterKey poster_key_for(const api::MediaItem& item, int width) {
  PosterKey key;
  key.width = width;
  if (!item.primary_image_tag.empty()) {
    key.item_id = item.id;
    key.tag = item.primary_image_tag;
  } else if (!item.series_primary_image_tag.empty() && !item.series_id.empty()) {
    key.item_id = item.series_id;
    key.tag = item.series_primary_image_tag;
  }
  return key;
}

std::string complete_server_address(const std::string& input) {
  std::string s;
  for (char c : input) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      s.push_back(c);
    }
  }
  if (s.empty()) {
    return s;
  }
  if (s.find("://") == std::string::npos) {
    s = "http://" + s;
  }
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

}  // namespace ui
