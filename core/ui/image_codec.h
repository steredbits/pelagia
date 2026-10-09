// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_IMAGE_CODEC_H
#define PELAGIA_CORE_UI_IMAGE_CODEC_H

// Decoding of posters (JPEG, PNG, WebP) and PNG encoding (screenshots)
// through libavcodec/libswscale: ffmpeg is already a dependency of the player, no
// additional image library to port to the PS5. Thread-safe (each
// call has its own contexts): used from the worker threads.

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// RGBA image (non-premultiplied alpha), w * 4 bytes per row.
struct Image {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> rgba;
};

enum class ImageFormat { Unknown, Jpeg, Png, WebP };

// Format recognized from the first bytes (file header).
ImageFormat sniff_image_format(const std::string& bytes);

// Decodes and downscales if needed to fit in max_w x max_h (0 = no limit),
// aspect ratio kept; never upscaled. False if the image is unreadable.
bool decode_image(const std::string& bytes, int max_w, int max_h, Image* out);

// Encodes as RGB PNG (alpha ignored: screenshots are opaque), maximum
// compression. False on failure.
bool encode_png(const Image& image, std::string* out);

// Size scaled down to fit in max_w x max_h, aspect ratio kept (pure function).
void fit_within(int w, int h, int max_w, int max_h, int* out_w, int* out_h);

}  // namespace ui

#endif  // PELAGIA_CORE_UI_IMAGE_CODEC_H
