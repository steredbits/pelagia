// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/subtitles.h"

#include <algorithm>
#include <cstdlib>

#include "util/text.h"

namespace util {

namespace {

// Reads a decimal number from *pos; false if there is no digit.
bool read_number(const std::string& s, size_t* pos, int64_t* out) {
  size_t i = *pos;
  int64_t v = 0;
  size_t digits = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 9) {
    v = v * 10 + (s[i] - '0');
    ++i;
    ++digits;
  }
  if (digits == 0) return false;
  *pos = i;
  *out = v;
  return true;
}

// "HH:MM:SS,mmm" (or ".", hours on 1 digit or more, milliseconds on 1 to 3 digits).
bool parse_time(const std::string& s, size_t* pos, int64_t* out_ms) {
  size_t i = *pos;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  int64_t h = 0, m = 0, sec = 0, ms = 0;
  if (!read_number(s, &i, &h) || i >= s.size() || s[i] != ':') return false;
  ++i;
  if (!read_number(s, &i, &m) || i >= s.size() || s[i] != ':') return false;
  ++i;
  if (!read_number(s, &i, &sec)) return false;
  if (i < s.size() && (s[i] == ',' || s[i] == '.')) {
    ++i;
    const size_t start = i;
    if (!read_number(s, &i, &ms)) return false;
    size_t digits = i - start;
    while (digits < 3) {  // ",5" = 500 ms
      ms *= 10;
      ++digits;
    }
    while (digits > 3) {
      ms /= 10;
      --digits;
    }
  }
  *pos = i;
  *out_ms = ((h * 60 + m) * 60 + sec) * 1000 + ms;
  return true;
}

// Removes HTML tags, {\...} blocks and decodes entities; \N -> line break.
std::string clean_cue_text(const std::string& raw) {
  std::string out;
  for (size_t i = 0; i < raw.size();) {
    const char c = raw[i];
    if (c == '<') {
      const size_t end = raw.find('>', i);
      const char next = i + 1 < raw.size() ? raw[i + 1] : '\0';
      const bool tag = next == '/' || (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z');
      if (tag && end != std::string::npos) {
        i = end + 1;
        continue;
      }
    } else if (c == '{' && i + 1 < raw.size() && raw[i + 1] == '\\') {
      const size_t end = raw.find('}', i);
      if (end != std::string::npos) {
        i = end + 1;
        continue;
      }
    } else if (c == '\\' && i + 1 < raw.size() && (raw[i + 1] == 'N' || raw[i + 1] == 'n')) {
      out += '\n';
      i += 2;
      continue;
    } else if (c == '&') {
      static const struct { const char* name; const char* value; } kEntities[] = {
          {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
          {"&apos;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
      bool done = false;
      for (const auto& e : kEntities) {
        const std::string name = e.name;
        if (raw.compare(i, name.size(), name) == 0) {
          out += e.value;
          i += name.size();
          done = true;
          break;
        }
      }
      if (done) continue;
    }
    out += c;
    ++i;
  }
  // Cleaned lines (invisible characters removed, edge spaces), empty lines removed.
  std::string result;
  size_t start = 0;
  while (start <= out.size()) {
    size_t end = out.find('\n', start);
    if (end == std::string::npos) end = out.size();
    const std::string line = clean_display_text(out.substr(start, end - start));
    if (!line.empty()) {
      if (!result.empty()) result += '\n';
      result += line;
    }
    start = end + 1;
  }
  return result;
}

}  // namespace

bool parse_srt(const std::string& data, std::vector<SubtitleCue>* out) {
  out->clear();
  std::string text;
  text.reserve(data.size());
  size_t begin = 0;
  if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) begin = 3;  // BOM
  for (size_t i = begin; i < data.size(); ++i) {
    if (data[i] == '\r') {
      text += '\n';
      if (i + 1 < data.size() && data[i + 1] == '\n') ++i;
    } else {
      text += data[i];
    }
  }

  size_t pos = 0;
  while (pos < text.size()) {
    // A block: lines up to the first empty line.
    std::vector<std::string> lines;
    while (pos < text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(pos, end - pos);
      pos = end + 1;
      if (line.find_first_not_of(" \t") == std::string::npos) {
        if (lines.empty()) continue;  // leading empty lines
        break;
      }
      lines.push_back(line);
    }
    // Timing line: one of the first two (the number is optional).
    size_t time_line = lines.size();
    for (size_t i = 0; i < lines.size() && i < 2; ++i) {
      if (lines[i].find("-->") != std::string::npos) {
        time_line = i;
        break;
      }
    }
    if (time_line == lines.size()) continue;
    const std::string& t = lines[time_line];
    size_t p = 0;
    int64_t start = 0, end = 0;
    if (!parse_time(t, &p, &start)) continue;
    const size_t arrow = t.find("-->", p);
    if (arrow == std::string::npos) continue;
    p = arrow + 3;
    if (!parse_time(t, &p, &end) || end <= start) continue;
    std::string raw;
    for (size_t i = time_line + 1; i < lines.size(); ++i) {
      if (!raw.empty()) raw += '\n';
      raw += lines[i];
    }
    SubtitleCue cue;
    cue.start_ms = start;
    cue.end_ms = end;
    cue.text = clean_cue_text(raw);
    if (!cue.text.empty()) out->push_back(cue);
  }
  std::stable_sort(out->begin(), out->end(), [](const SubtitleCue& a, const SubtitleCue& b) {
    return a.start_ms < b.start_ms;
  });
  return !out->empty();
}

void SubtitleTrack::set_cues(std::vector<SubtitleCue> cues) {
  cues_ = std::move(cues);
  std::stable_sort(cues_.begin(), cues_.end(), [](const SubtitleCue& a, const SubtitleCue& b) {
    return a.start_ms < b.start_ms;
  });
  max_duration_ms_ = 0;
  for (const SubtitleCue& c : cues_) {
    max_duration_ms_ = std::max(max_duration_ms_, c.end_ms - c.start_ms);
  }
}

std::vector<const SubtitleCue*> SubtitleTrack::active_at(int64_t position_ms) const {
  std::vector<const SubtitleCue*> active;
  // First cue that starts after the position, then walk backwards as long as an
  // older cue (even a very long one) could still cover the position.
  size_t hi = std::upper_bound(cues_.begin(), cues_.end(), position_ms,
                               [](int64_t pos, const SubtitleCue& c) { return pos < c.start_ms; }) -
              cues_.begin();
  for (size_t i = hi; i > 0; --i) {
    const SubtitleCue& c = cues_[i - 1];
    if (c.start_ms + max_duration_ms_ <= position_ms) break;
    if (position_ms < c.end_ms) active.push_back(&c);
  }
  std::reverse(active.begin(), active.end());
  return active;
}

}  // namespace util
