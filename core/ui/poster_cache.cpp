// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/poster_cache.h"

#include <cstdio>

#include "util/log.h"

namespace ui {

std::string PosterKey::str() const {
  char width_text[16];
  std::snprintf(width_text, sizeof(width_text), "_w%d", width);
  return item_id + "_" + tag + width_text;
}

PosterCache::PosterCache(Canvas* canvas, TaskRunner* io, MainQueue* main,
                         PosterFetcher* fetcher, util::DiskCache* disk,
                         const Options& options)
    : canvas_(canvas), io_(io), main_(main), fetcher_(fetcher), disk_(disk),
      options_(options) {}

PosterCache::~PosterCache() {
  for (auto& item : entries_) {
    if (item.second.wanted) {
      *item.second.wanted = false;  // tasks not started yet: nothing to download
    }
    release(&item.second);
  }
}

void PosterCache::release(Entry* entry) {
  if (entry->poster.texture != 0) {
    canvas_->destroy_texture(entry->poster.texture);
    memory_bytes_ -= static_cast<size_t>(entry->poster.w) * entry->poster.h * 4;
    entry->poster = Poster{};
  }
}

const Poster* PosterCache::get(const PosterKey& key) {
  if (key.item_id.empty() || key.tag.empty() || key.width <= 0) {
    return nullptr;  // no poster on the server side
  }
  const std::string id = key.str();
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    it = entries_.emplace(id, Entry()).first;
    start_load(id, key, &it->second);
  }
  Entry& e = it->second;
  e.last_frame = frame_;
  switch (e.state) {
    case State::Ready:
      return &e.poster;
    case State::Failed:
      if (now_ms_ - e.failed_at_ms >= options_.retry_failed_ms) {
        start_load(id, key, &e);
      }
      return nullptr;
    default:
      return nullptr;
  }
}

void PosterCache::start_load(const std::string& id, const PosterKey& key, Entry* entry) {
  entry->state = State::Loading;
  entry->serial = next_serial_++;
  entry->wanted = std::make_shared<std::atomic<bool>>(true);
  ++loading_;
  ++loads_;
  const uint64_t serial = entry->serial;
  std::shared_ptr<std::atomic<bool>> wanted = entry->wanted;
  PosterFetcher* fetcher = fetcher_;
  util::DiskCache* disk = disk_;
  MainQueue* main = main_;
  const Lifetime::Token token = lifetime_.token();
  PosterCache* self = this;
  io_->post([=]() {
    if (!wanted->load()) {
      return;  // no longer displayed: request abandoned before any access
    }
    const std::string disk_key = key.str();
    std::string bytes;
    bool ok = disk && disk->get(disk_key, &bytes);
    const bool from_disk = ok;
    if (!ok) {
      ok = fetcher->fetch(key, &bytes);
    }
    std::shared_ptr<Image> image = std::make_shared<Image>();
    if (!ok || !decode_image(bytes, key.width, key.width * 2, image.get())) {
      if (ok) {
        LOG_DEBUG("Unreadable poster: %s", disk_key.c_str());
        if (disk && from_disk) {
          disk->remove(disk_key);
        }
      }
      image.reset();
    } else if (disk && !from_disk) {
      disk->put(disk_key, bytes);
    }
    main->post([=]() {
      if (Lifetime::alive(token)) {
        self->on_loaded(id, serial, image);
      }
    });
  });
}

void PosterCache::on_loaded(const std::string& id, uint64_t serial,
                            std::shared_ptr<Image> image) {
  auto it = entries_.find(id);
  if (it == entries_.end() || it->second.serial != serial ||
      it->second.state != State::Loading) {
    return;  // request abandoned in the meantime
  }
  Entry& e = it->second;
  --loading_;
  e.wanted.reset();
  if (!image) {
    e.state = State::Failed;
    e.failed_at_ms = now_ms_;
    return;
  }
  e.state = State::Decoded;
  e.image = image;
  decoded_.push_back(id);
}

void PosterCache::upload_pending() {
  int budget = options_.uploads_per_frame;
  size_t consumed = 0;
  for (; consumed < decoded_.size() && budget > 0; ++consumed) {
    auto it = entries_.find(decoded_[consumed]);
    if (it == entries_.end() || it->second.state != State::Decoded) {
      continue;
    }
    Entry& e = it->second;
    const Image& img = *e.image;
    const TextureId tex = canvas_->create_texture(img.w, img.h, PixelFormat::Rgba8);
    if (tex == 0 ||
        !canvas_->update_texture(tex, 0, 0, img.w, img.h, img.rgba.data(), img.w * 4)) {
      if (tex != 0) {
        canvas_->destroy_texture(tex);
      }
      e.state = State::Failed;
      e.failed_at_ms = now_ms_;
    } else {
      e.state = State::Ready;
      e.poster.texture = tex;
      e.poster.w = img.w;
      e.poster.h = img.h;
      memory_bytes_ += static_cast<size_t>(img.w) * img.h * 4;
    }
    e.image.reset();
    --budget;
  }
  decoded_.erase(decoded_.begin(), decoded_.begin() + static_cast<long>(consumed));
}

void PosterCache::evict() {
  while (memory_bytes_ > options_.memory_budget) {
    // Least recently drawn, excluding posters drawn in the previous frame
    // (visible on screen).
    auto victim = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.state == State::Ready && it->second.last_frame + 1 < frame_ &&
          (victim == entries_.end() || it->second.last_frame < victim->second.last_frame)) {
        victim = it;
      }
    }
    if (victim == entries_.end()) {
      return;  // everything is visible: temporary overrun accepted
    }
    release(&victim->second);
    entries_.erase(victim);
  }
}

void PosterCache::begin_frame(uint64_t now_ms) {
  ++frame_;
  now_ms_ = now_ms;
  for (auto it = entries_.begin(); it != entries_.end();) {
    Entry& e = it->second;
    if (e.state == State::Loading &&
        frame_ - e.last_frame > static_cast<uint64_t>(options_.unwanted_after_frames)) {
      *e.wanted = false;
      --loading_;
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  upload_pending();
  evict();
}

bool PosterCache::busy() const { return loading_ > 0 || !decoded_.empty(); }

size_t PosterCache::texture_count() const {
  size_t n = 0;
  for (const auto& item : entries_) {
    n += item.second.poster.texture != 0;
  }
  return n;
}

}  // namespace ui
