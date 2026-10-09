// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Generates a small test MP4: media_synth_tool <output.mp4> <seconds> <av|video|audio>
// Used by the pelagia-play CLI test (play_cli_test.cmake).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "media_synth.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "Usage : media_synth_tool <sortie.mp4> <secondes> <av|video|audio>\n");
    return 2;
  }
  testmedia::SynthSpec spec;
  spec.seconds = std::atof(argv[2]);
  if (std::strcmp(argv[3], "video") == 0) {
    spec.with_audio = false;
  } else if (std::strcmp(argv[3], "audio") == 0) {
    spec.with_video = false;
  } else if (std::strcmp(argv[3], "av") != 0) {
    std::fprintf(stderr, "Type inconnu : %s\n", argv[3]);
    return 2;
  }
  return testmedia::synth_media(argv[1], spec) ? 0 : 1;
}
