// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_POSTER_CACHE_H
#define PELAGIA_CORE_UI_POSTER_CACHE_H

// Two-level poster cache:
// - memory: textures ready to draw, budget in bytes (RGBA), LRU eviction
//   of posters not drawn in the current frame;
// - disk (util::DiskCache): files as served by the server.
//
// get() is called during drawing (main thread): it never blocks.
// A missing poster is requested from a worker thread (disk, otherwise
// network, then decoding and downscaling); the decoded image comes back through the
// MainQueue and becomes a texture at the start of a later frame (a few
// uploads per frame at most, to avoid hitches). A request that has not
// been displayed for a while (fast scrolling) is abandoned before the
// download.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/canvas.h"
#include "ui/image_codec.h"
#include "ui/tasks.h"
#include "util/disk_cache.h"

namespace ui {

struct PosterKey {
  std::string item_id;
  std::string tag;  // ImageTags.Primary: changes when the image changes
  int width = 0;    // width requested from the server and decoded width
  std::string str() const;
};

// Download of the bytes of a poster (called from a worker thread).
class PosterFetcher {
 public:
  virtual ~PosterFetcher() {}
  virtual bool fetch(const PosterKey& key, std::string* bytes) = 0;
};

struct Poster {
  TextureId texture = 0;
  int w = 0;
  int h = 0;
};

class PosterCache {
 public:
  struct Options {
    size_t memory_budget = 64u * 1024 * 1024;  // ~190 posters of 240x360
    int uploads_per_frame = 4;
    int unwanted_after_frames = 30;      // ~0.5 s without being drawn
    uint64_t retry_failed_ms = 30000;    // new attempt after a failure
  };

  // disk may be null (disk cache unavailable).
  PosterCache(Canvas* canvas, TaskRunner* io, MainQueue* main, PosterFetcher* fetcher,
              util::DiskCache* disk, const Options& options);
  ~PosterCache();

  // Start of frame: textures to create, eviction, abandonment of forgotten requests.
  void begin_frame(uint64_t now_ms);

  // Poster ready, or nullptr (loading started if needed, or failed: the caller
  // draws a placeholder frame).
  const Poster* get(const PosterKey& key);

  // Loads in progress or textures waiting to be created.
  bool busy() const;
  size_t memory_bytes() const { return memory_bytes_; }
  size_t texture_count() const;
  uint64_t load_count() const { return loads_; }

 private:
  enum class State : uint8_t { Loading, Decoded, Ready, Failed };
  struct Entry {
    State state = State::Loading;
    uint64_t serial = 0;
    uint64_t last_frame = 0;  // last frame where get() asked for it
    uint64_t failed_at_ms = 0;
    std::shared_ptr<std::atomic<bool>> wanted;
    std::shared_ptr<Image> image;  // Decoded: waiting for a texture
    Poster poster;
  };

  void start_load(const std::string& id, const PosterKey& key, Entry* entry);
  void on_loaded(const std::string& id, uint64_t serial, std::shared_ptr<Image> image);
  void upload_pending();
  void evict();
  void release(Entry* entry);

  Canvas* canvas_;
  TaskRunner* io_;
  MainQueue* main_;
  PosterFetcher* fetcher_;
  util::DiskCache* disk_;
  Options options_;
  std::unordered_map<std::string, Entry> entries_;
  std::vector<std::string> decoded_;  // arrival order of the decoded images
  uint64_t frame_ = 1;
  uint64_t now_ms_ = 0;
  uint64_t next_serial_ = 1;
  size_t memory_bytes_ = 0;
  size_t loading_ = 0;
  uint64_t loads_ = 0;  // loads started (disk or network)
  Lifetime lifetime_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_POSTER_CACHE_H
