// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/backend.h"

#include <algorithm>

#include "ui/format.h"

namespace ui {

std::vector<api::Library> supported_libraries(const std::vector<api::Library>& all) {
  std::vector<api::Library> out;
  for (const api::Library& lib : all) {
    if (lib.collection_type == "movies" || lib.collection_type == "tvshows" ||
        lib.collection_type.empty()) {
      out.push_back(lib);
    }
  }
  return out;
}

std::string item_types_for(const api::Library& library) {
  if (library.collection_type == "movies") {
    return "Movie";
  }
  if (library.collection_type == "tvshows") {
    return "Series";
  }
  return "Movie,Series";
}

void sort_for_display(std::vector<api::MediaItem>* items) {
  // Keys computed once (UTF-8 cleaning is not free).
  std::vector<std::pair<std::string, size_t>> keys;
  keys.reserve(items->size());
  for (size_t i = 0; i < items->size(); ++i) {
    keys.emplace_back(item_sort_key((*items)[i]), i);
  }
  std::stable_sort(keys.begin(), keys.end(),
                   [](const std::pair<std::string, size_t>& a,
                      const std::pair<std::string, size_t>& b) { return a.first < b.first; });
  std::vector<api::MediaItem> sorted;
  sorted.reserve(items->size());
  for (const auto& k : keys) {
    sorted.push_back(std::move((*items)[k.second]));
  }
  items->swap(sorted);
}

}  // namespace ui
