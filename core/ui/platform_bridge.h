// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_PLATFORM_BRIDGE_H
#define PELAGIA_CORE_UI_PLATFORM_BRIDGE_H

// Implementations of the Canvas, the FileStore and the player sinks on top of
// platform.h. Compiled in a separate library (pelagia_ui_platform),
// linked to the platform layer: the rest of the core and the tests do not depend
// on it.

#include "player/player.h"
#include "ui/canvas.h"
#include "util/file_store.h"

namespace ui {

class PlatformCanvas final : public Canvas {
 public:
  TextureId create_texture(int w, int h, PixelFormat format) override;
  bool update_texture(TextureId id, int x, int y, int w, int h, const uint8_t* pixels,
                      int pitch) override;
  void destroy_texture(TextureId id) override;
  void fill_rect(const Rect& r, Color color) override;
  void draw_texture(TextureId id, const Rect& src, const Rect& dst, Color tint) override;
  void set_clip(const Rect* clip) override;
  bool submit_video_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                        int stride_y, int stride_u, int stride_v, int w, int h) override;
  void draw_video(const Rect& area) override;
  void clear_video() override;
};

class PlatformFileStore final : public util::FileStore {
 public:
  bool make_dirs(const std::string& path) override;
  bool read(const std::string& path, std::string* out) override;
  bool write(const std::string& path, const std::string& data, bool private_file) override;
  bool remove(const std::string& path) override;
  bool touch(const std::string& path) override;
  bool list(const std::string& dir, std::vector<util::StoredFile>* out) override;
};

// Player audio output to platform::audio_*.
struct PlatformAudioSink final : player::AudioSink {
  size_t queue(const void* samples, size_t bytes) override;
  size_t queued_bytes() override;
  void pause(bool paused) override;
  void flush() override;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_PLATFORM_BRIDGE_H
