// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_SUBTITLES_H
#define PELAGIA_CORE_UTIL_SUBTITLES_H

// Text subtitles rendered by the client: SRT parsing and lookup of the
// lines active at a position. The server also serves ASS/SSA tracks as
// SRT (ffmpeg conversion: <i> <b> and {\an8} tags kept); the style
// is ignored, only the text counts (libass: not handled for now). Pure functions.

#include <cstdint>
#include <string>
#include <vector>

namespace util {

struct SubtitleCue {
  int64_t start_ms = 0;
  int64_t end_ms = 0;
  std::string text;  // lines separated by '\n', tags removed, no empty line
};

// Parses an SRT file (UTF-8, BOM and CRLF/CR line endings accepted, optional
// cue numbers, "," or "." before the milliseconds). Unreadable
// blocks are ignored. Removes <i> <b> <u> <font ...>, {\...} blocks, \N
// (ASS line break) and decodes &amp; &lt; &gt; &quot; &nbsp;. False if no
// cue could be read; out is then empty. Cues are sorted by start.
bool parse_srt(const std::string& data, std::vector<SubtitleCue>* out);

// Cues of a track, queryable by position.
class SubtitleTrack {
 public:
  void set_cues(std::vector<SubtitleCue> cues);
  size_t size() const { return cues_.size(); }
  const std::vector<SubtitleCue>& cues() const { return cues_; }

  // Cues displayed at position_ms (start included, end excluded), by increasing start.
  std::vector<const SubtitleCue*> active_at(int64_t position_ms) const;

 private:
  std::vector<SubtitleCue> cues_;
  int64_t max_duration_ms_ = 0;
};

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_SUBTITLES_H
