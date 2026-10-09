// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_TESTS_MEDIA_SYNTH_H
#define PELAGIA_TESTS_MEDIA_SYNTH_H

// Synthesis of small MP4 test files through libavformat/libavcodec
// (programmed pattern + sine wave), without depending on the ffmpeg binary.
// Codecs: mpeg4 + native aac, always built into libavcodec - the player
// is codec-agnostic, the H.264 target is validated by the CI smoke test.

namespace testmedia {

struct SynthSpec {
  double seconds = 2.0;
  bool with_video = true;
  bool with_audio = true;
  int width = 160;
  int height = 120;
  int fps = 25;
  int sample_rate = 48000;
};

bool synth_media(const char* path, const SynthSpec& spec);

}  // namespace testmedia

#endif  // PELAGIA_TESTS_MEDIA_SYNTH_H
