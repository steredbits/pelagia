// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/screens/player_screen.h"

#include "ui/app.h"
#include "ui/format.h"
#include "ui/theme.h"
#include "ui/track_menu.h"
#include "util/log.h"
#include "util/text.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

constexpr uint64_t kOverlayMs = 4000;
constexpr int64_t kShortSeekMs = 10000;
constexpr int64_t kLongSeekMs = 60000;

}  // namespace

bool PlayerScreen::CanvasVideoSink::submit(const AVFrame* frame) {
  return canvas->submit_video_yuv(frame->data[0], frame->data[1], frame->data[2],
                                  frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                  frame->width, frame->height);
}

PlayerScreen::PlayerScreen(const api::MediaItem& item, int64_t start_ms,
                           const api::TrackSelection& tracks)
    : item_(item), start_ms_(start_ms), initial_tracks_(tracks) {}

PlayerScreen::~PlayerScreen() {
  // Application closing during playback: the session stops
  // here synchronously (stop report included).
  session_.reset();
}

void PlayerScreen::enter(App& app) {
  video_sink_.canvas = &app.canvas();
  app.canvas().clear_video();  // no image from a previous playback
  last_input_ms_ = app.now();
  std::unique_ptr<PlaybackMedia> media = app.backend().create_playback(item_, start_ms_, initial_tracks_);
  if (!media) {
    no_media_ = true;
    return;
  }
  session_.reset(new PlaybackSession(std::move(media), item_, start_ms_));
  session_->begin(&video_sink_, app.audio());
  sync_subtitles(app);
}

bool PlayerScreen::opening() const {
  return session_ && session_->phase() == PlaybackSession::Phase::Opening;
}

bool PlayerScreen::open_failed() const {
  return no_media_ || (session_ && session_->phase() == PlaybackSession::Phase::OpenFailed);
}

bool PlayerScreen::shows_video() const {
  return session_ && session_->phase() == PlaybackSession::Phase::Playing &&
         session_->player().has_video();
}

bool PlayerScreen::busy() const {
  if (opening()) {
    return true;
  }
  if (!session_ || session_->phase() != PlaybackSession::Phase::Playing) {
    return false;
  }
  return session_->player().is_buffering() || session_->player().seek_in_flight();
}

bool PlayerScreen::overlay_visible(uint64_t now_ms) const {
  if (!session_ || session_->phase() != PlaybackSession::Phase::Playing) {
    return true;
  }
  const player::Player& p = session_->player();
  return p.state() == player::PlayerState::Paused || p.is_buffering() || p.seek_in_flight() ||
         network_failed_ || now_ms - last_input_ms_ < kOverlayMs;
}

void PlayerScreen::update(App& app) {
  if (!session_ || finishing_) {
    return;
  }
  if (track_change_ && session_->phase() == PlaybackSession::Phase::Playing &&
      !session_->player().seek_in_flight()) {
    track_change_ = false;  // stream reopened, playback resumed
  }
  const player::Player::TickResult r = session_->tick(app.now());
  if (r.new_video_frame) {
    app.mark_dirty();
  }
  if (r.failed) {
    network_failed_ = true;
  }
  if (r.ended) {
    finish(app);  // end of the movie: back to the page
  }
}

void PlayerScreen::finish(App& app) {
  if (finishing_) {
    return;
  }
  finishing_ = true;
  if (session_) {
    app.retire_playback(std::move(session_));  // stop + report in the background
  }
  app.canvas().clear_video();
  app.pop();
}

void PlayerScreen::seek(int64_t delta_ms) {
  if (session_ && session_->phase() == PlaybackSession::Phase::Playing) {
    session_->player().seek_relative(delta_ms);
  }
}

void PlayerScreen::handle(App& app, const platform::Input& in) {
  last_input_ms_ = app.now();
  app.mark_dirty();
  if (in.event == InputEvent::Back) {
    finish(app);
    return;
  }
  if (!session_ || session_->phase() != PlaybackSession::Phase::Playing) {
    return;  // opening in progress or failed: only Back acts
  }
  player::Player& p = session_->player();
  switch (in.event) {
    case InputEvent::Ok:
    case InputEvent::PlayPause:
      if (network_failed_) {
        // A seek starts a new recovery attempt.
        network_failed_ = false;
        p.seek_to(p.position_ms());
      } else {
        p.toggle_pause();
      }
      break;
    case InputEvent::Left:
    case InputEvent::SeekBack: seek(-kShortSeekMs); break;
    case InputEvent::Right:
    case InputEvent::SeekFwd: seek(kShortSeekMs); break;
    case InputEvent::SeekBackLong: seek(-kLongSeekMs); break;
    case InputEvent::SeekFwdLong: seek(kLongSeekMs); break;
    case InputEvent::Menu: open_options(app); break;
    default: break;  // up, down: show the banner
  }
}

// Source of the item for the current tracks, nullptr if the media has nothing to choose.
const api::MediaSourceInfo* PlayerScreen::track_source(api::TrackSelection* current) const {
  if (!session_ || !session_->tracks(current)) {
    return nullptr;
  }
  const api::MediaSourceInfo* src = api::find_source(item_, current->media_source_id);
  return src && has_track_choices(*src) ? src : nullptr;
}

void PlayerScreen::open_options(App& app) {
  api::TrackSelection cur;
  const api::MediaSourceInfo* src = track_source(&cur);
  if (!src) {
    app.toast(util::tr(util::Str::NoTrackChoice));
    return;
  }
  std::vector<MenuItem> items;
  items.push_back(MenuItem{util::trf(util::Str::PlayerAudioItem, audio_summary(*src, cur).c_str()),
                           [this](App& a) { open_audio_list(a); }});
  items.push_back(MenuItem{util::trf(util::Str::PlayerSubtitleItem, subtitle_summary(*src, cur).c_str()),
                           [this](App& a) { open_subtitle_list(a); }});
  app.open_menu(util::tr(util::Str::Options), std::move(items));
}

void PlayerScreen::open_audio_list(App& app) {
  api::TrackSelection cur;
  const api::MediaSourceInfo* src = track_source(&cur);
  if (!src) return;
  const api::UserPreferences prefs = app.backend().preferences();
  int initial = 0;
  std::vector<MenuItem> items = track_menu_items(
      audio_choices(*src, cur),
      [this, prefs](App& a, int index) {
        api::TrackSelection now;
        if (const api::MediaSourceInfo* s = track_source(&now)) {
          apply_tracks(a, api::with_audio(*s, prefs, now, index));
        }
      },
      &initial);
  app.open_menu(util::tr(util::Str::Audio), std::move(items), initial);
}

void PlayerScreen::open_subtitle_list(App& app) {
  api::TrackSelection cur;
  const api::MediaSourceInfo* src = track_source(&cur);
  if (!src) return;
  int initial = 0;
  std::vector<MenuItem> items = track_menu_items(
      subtitle_choices(*src, cur),
      [this](App& a, int index) {
        api::TrackSelection now;
        if (const api::MediaSourceInfo* s = track_source(&now)) {
          apply_tracks(a, api::with_subtitle(*s, now, index));
        }
      },
      &initial);
  app.open_menu(util::tr(util::Str::Subtitles), std::move(items), initial);
}

void PlayerScreen::apply_tracks(App& app, const api::TrackSelection& next) {
  if (!session_ || !session_->change_tracks(next)) {
    return;
  }
  // Stream reopened (audio, burn-in): same wait as a seek, named differently.
  track_change_ = session_->player().seek_in_flight();
  sync_subtitles(app);
}

// Subtitle track to be displayed by the client according to the current tracks: it
// downloads it (once per track, outside the display loop) if needed.
void PlayerScreen::sync_subtitles(App& app) {
  api::TrackSelection tracks;
  wanted_subtitle_ = -1;
  if (!session_ || !session_->tracks(&tracks) ||
      tracks.subtitle_delivery != api::SubtitleDelivery::Client || tracks.subtitle_index < 0) {
    return;
  }
  const int index = tracks.subtitle_index;
  wanted_subtitle_ = index;
  if (subtitle_tracks_.count(index) != 0 || loading_subtitle_ == index) {
    return;
  }
  loading_subtitle_ = index;
  const std::string item_id = item_.id;
  const std::string source_id = tracks.media_source_id;
  struct SubtitleResult {
    api::ApiResult result;
    std::vector<util::SubtitleCue> cues;
  };
  app.run<SubtitleResult>(
      lifetime(),
      [item_id, source_id, index](Backend& b, SubtitleResult& r) {
        std::string srt;
        r.result = b.subtitle_text(item_id, source_id, index, &srt);
        if (r.result.ok() && !util::parse_srt(srt, &r.cues)) {
          r.cues.clear();  // track without lines: nothing to display, not an error
        }
      },
      [this, &app, index](SubtitleResult& r) {
        if (loading_subtitle_ == index) loading_subtitle_ = -1;
        if (!r.result.ok()) {
          LOG_WARN("Subtitles of track %d unavailable: %s", index,
                   r.result.message.c_str());
          app.toast(util::tr(util::Str::SubtitlesUnavailable), true);
          return;
        }
        util::SubtitleTrack track;
        track.set_cues(std::move(r.cues));
        LOG_INFO("Subtitles of track %d loaded: %zu cues", index, track.size());
        subtitle_tracks_[index] = std::move(track);
        app.mark_dirty();
      });
}

std::vector<std::string> PlayerScreen::subtitle_lines(int64_t position_ms) const {
  std::vector<std::string> lines;
  const auto it = subtitle_tracks_.find(wanted_subtitle_);
  if (wanted_subtitle_ < 0 || it == subtitle_tracks_.end()) {
    return lines;
  }
  for (const util::SubtitleCue* cue : it->second.active_at(position_ms)) {
    size_t start = 0;
    while (start <= cue->text.size()) {
      size_t end = cue->text.find('\n', start);
      if (end == std::string::npos) end = cue->text.size();
      lines.push_back(cue->text.substr(start, end - start));
      start = end + 1;
    }
  }
  return lines;
}

// Active lines on a dark background, centered above the banner (which
// pushes them up when it is displayed).
void PlayerScreen::draw_subtitles(Painter& p) {
  const std::vector<std::string> lines = subtitle_lines(session_->player().position_ms());
  if (lines.empty()) {
    return;
  }
  constexpr int kPx = 44;
  const int line_h = p.text().line_height(Weight::Regular, kPx) + 8;
  int y = (overlay_visible(p.now()) ? 820 : theme::kScreenH - 110) -
          static_cast<int>(lines.size()) * line_h;
  for (const std::string& line : lines) {
    const int w = p.text().measure(line, Weight::Regular, kPx);
    const int box_w = (w > theme::kContentW - 80 ? theme::kContentW - 80 : w) + 40;
    const Rect box = rect((theme::kScreenW - box_w) / 2, y, box_w, line_h);
    p.fill(box, rgba(0, 0, 0, 150));
    p.label_in(line, rect(box.x + 20, box.y, box.w - 40, box.h), Weight::Regular, kPx,
               theme::text(), Align::Center);
    y += line_h;
  }
}

void PlayerScreen::draw(App& app, Painter& p) {
  const Rect screen = rect(0, 0, theme::kScreenW, theme::kScreenH);
  if (shows_video()) {
    p.canvas().draw_video(screen);
  } else {
    p.fill(screen, rgba(0, 0, 0));
  }
  const std::string title = display_title(item_);
  if (open_failed()) {
    p.label_in(title, rect(0, 380, theme::kScreenW, 80), Weight::Bold, theme::kTitle,
               theme::text(), Align::Center);
    p.status(rect(0, 480, theme::kScreenW, 100),
             util::tr(util::Str::PlaybackFailed),
             false, true);
    p.hints(util::tr(util::Str::HintBack));
    return;
  }
  if (opening()) {
    p.label_in(title, rect(0, 330, theme::kScreenW, 80), Weight::Bold, theme::kTitle,
               theme::text(), Align::Center);
    std::string text = util::tr(util::Str::Loading);
    if (start_ms_ > 0) text = util::trf(util::Str::LoadingResume, format_clock(start_ms_).c_str());
    p.status(rect(0, 500, theme::kScreenW, 120), text, true, false);
    p.label_in(util::tr(util::Str::ServerPreparing),
               rect(0, 640, theme::kScreenW, 40), Weight::Regular, theme::kSmall,
               theme::text_dim(), Align::Center);
    p.hints(util::tr(util::Str::HintCancel));
    return;
  }
  if (session_ && session_->phase() == PlaybackSession::Phase::Playing) {
    draw_subtitles(p);
  }
  if (session_ && overlay_visible(app.now())) {
    draw_overlay(app, p);
  }
}

void PlayerScreen::draw_overlay(App& app, Painter& p) {
  (void)app;
  const player::Player& pl = session_->player();
  // Haut : titre.
  p.fill(rect(0, 0, theme::kScreenW, 170), rgba(0, 0, 0, 150));
  if (item_.type == "Episode") {
    p.label_in(util::clean_display_text(item_.series_name) + "  ·  " + episode_code(item_),
               rect(theme::kMarginX, 34, theme::kContentW, 40), Weight::Regular, theme::kBody,
               theme::text_dim());
  }
  p.label_in(display_title(item_), rect(theme::kMarginX, 74, theme::kContentW, 70), Weight::Bold,
             44, theme::text());

  // Center: waiting state, pause or failure.
  const int cx = theme::kScreenW / 2;
  const int64_t position = pl.position_ms();
  if (network_failed_ || pl.state() == player::PlayerState::Failed) {
    p.fill(rect(cx - 520, 420, 1040, 170), rgba(0, 0, 0, 190));
    // Stream open but nothing readable within the delay (e.g. audio track not found):
    // different message from a network drop.
    const bool stalled = pl.failure_kind() == player::FailureKind::Stalled;
    p.label_in(stalled ? util::tr(util::Str::ServerSendsNothing) : util::tr(util::Str::ConnectionLost),
               rect(cx - 500, 440, 1000, 60), Weight::Bold, theme::kHeading, theme::danger(),
               Align::Center);
    p.label_in(util::tr(util::Str::RetryQuitHint), rect(cx - 500, 510, 1000, 50),
               Weight::Regular, theme::kBody, theme::text(), Align::Center);
  } else if (pl.seek_in_flight()) {
    p.fill(rect(cx - 330, 430, 660, 190), rgba(0, 0, 0, 170));
    p.spinner(cx, 490, 30, theme::text());
    const std::string text = track_change_ ? util::trf(util::Str::ChangingTrack, format_clock(position).c_str())
                                           : util::trf(util::Str::Seeking, format_clock(position).c_str());
    p.label_in(text, rect(cx - 320, 545, 640, 50), Weight::Regular, theme::kBody, theme::text(),
               Align::Center);
  } else if (pl.is_buffering()) {
    p.fill(rect(cx - 330, 430, 660, 190), rgba(0, 0, 0, 170));
    p.spinner(cx, 490, 30, theme::text());
    p.label_in(util::tr(util::Str::Buffering), rect(cx - 320, 545, 640, 50), Weight::Regular,
               theme::kBody, theme::text(), Align::Center);
  } else if (pl.state() == player::PlayerState::Paused) {
    p.fill(rect(cx - 70, 470, 140, 140), rgba(0, 0, 0, 150));
    p.pause_icon(rect(cx - 30, 500, 60, 80), theme::text());
  }

  // Bottom: state, time, progress bar.
  p.fill(rect(0, 840, theme::kScreenW, 240), rgba(0, 0, 0, 160));
  const int64_t duration =
      pl.duration_ms() > 0 ? pl.duration_ms() : item_.runtime_ticks / kTicksPerMs;
  const Rect icon = rect(theme::kMarginX, 880, 30, 36);
  if (pl.state() == player::PlayerState::Paused) {
    p.pause_icon(icon, theme::text());
  } else {
    p.play_icon(icon, theme::text());
  }
  p.label(format_clock(position), theme::kMarginX + 56, 874, Weight::Bold, theme::kBody,
          theme::text());
  p.label_in(format_clock(duration), rect(theme::kMarginX, 874, theme::kContentW, 44),
             Weight::Regular, theme::kBody, theme::text_dim(), Align::Right);
  const Rect bar = rect(theme::kMarginX, 940, theme::kContentW, 10);
  const double fraction = duration > 0 ? static_cast<double>(position) / duration : 0.0;
  p.progress(bar, fraction, theme::accent(), rgba(255, 255, 255, 60));
  const int marker = bar.x + static_cast<int>(bar.w * (fraction > 1 ? 1 : fraction));
  p.fill(rect(marker - 9, bar.y - 9, 18, 28), theme::text());
  // Current tracks (reminder of the Options menu).
  api::TrackSelection tracks;
  if (const api::MediaSourceInfo* src = track_source(&tracks)) {
    p.label_in(util::trf(util::Str::PlayerBanner, audio_summary(*src, tracks).c_str(),
                       subtitle_summary(*src, tracks).c_str()),
               rect(theme::kMarginX, 960, theme::kContentW, 32), Weight::Regular, theme::kSmall,
               theme::text_dim());
  }
  p.hints(util::tr(util::Str::PlayerHint));
}

}  // namespace ui
