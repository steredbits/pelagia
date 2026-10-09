// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/platform_bridge.h"

#include "platform.h"

namespace ui {

TextureId PlatformCanvas::create_texture(int w, int h, PixelFormat format) {
  return platform::texture_create(w, h, format);
}

bool PlatformCanvas::update_texture(TextureId id, int x, int y, int w, int h,
                                    const uint8_t* pixels, int pitch) {
  return platform::texture_update(id, x, y, w, h, pixels, pitch);
}

void PlatformCanvas::destroy_texture(TextureId id) { platform::texture_destroy(id); }

void PlatformCanvas::fill_rect(const Rect& r, Color color) { platform::draw_rect(r, color); }

void PlatformCanvas::draw_texture(TextureId id, const Rect& src, const Rect& dst,
                                  Color tint) {
  platform::draw_texture(id, src, dst, tint);
}

void PlatformCanvas::set_clip(const Rect* clip) { platform::set_clip(clip); }

bool PlatformCanvas::submit_video_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                      int stride_y, int stride_u, int stride_v, int w,
                                      int h) {
  return platform::video_submit_frame_yuv(y, u, v, stride_y, stride_u, stride_v, w, h);
}

void PlatformCanvas::draw_video(const Rect& area) { platform::draw_video(area); }

void PlatformCanvas::clear_video() { platform::video_clear(); }

bool PlatformFileStore::make_dirs(const std::string& path) { return platform::make_dirs(path); }

bool PlatformFileStore::read(const std::string& path, std::string* out) {
  return platform::file_read(path, out);
}

bool PlatformFileStore::write(const std::string& path, const std::string& data,
                              bool private_file) {
  return platform::file_write(path, data, private_file);
}

bool PlatformFileStore::remove(const std::string& path) { return platform::file_remove(path); }

bool PlatformFileStore::touch(const std::string& path) { return platform::file_touch(path); }

bool PlatformFileStore::list(const std::string& dir, std::vector<util::StoredFile>* out) {
  std::vector<platform::FileInfo> files;
  if (!platform::list_dir(dir, &files)) {
    return false;
  }
  out->clear();
  for (const platform::FileInfo& f : files) {
    util::StoredFile s;
    s.name = f.name;
    s.size = f.size;
    s.mtime_s = f.mtime_s;
    out->push_back(s);
  }
  return true;
}

size_t PlatformAudioSink::queue(const void* samples, size_t bytes) {
  return platform::audio_queue(samples, bytes);
}

size_t PlatformAudioSink::queued_bytes() { return platform::audio_queued_bytes(); }

void PlatformAudioSink::pause(bool paused) { platform::audio_pause(paused); }

void PlatformAudioSink::flush() { platform::audio_flush(); }

}  // namespace ui
