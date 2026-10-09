// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_MEDIA_SOURCE_H
#define PELAGIA_CORE_PLAYER_MEDIA_SOURCE_H

// Abstract playback source. The pipeline (demux, queues, decoding, sync)
// does not know how to seek: only the source does.
// - FileSource (local file, av_seek_frame, absolute PTS).
// - Jellyfin network source - seek by reopening the stream with
//   startTimeTicks + nouveau playSessionId ; timestamps_absolute() == false,
//   the PTS of the new stream are realigned by SeekOffsetTracker.

#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
}

namespace player {

// True = abort the blocking operation in progress (player stop, or a
// more recent pending seek). Called from the thread that blocks.
using InterruptFn = bool (*)(void* ctx);

class MediaSource {
 public:
  virtual ~MediaSource() {}

  // Plugs in the player's interruption test, before open(). A network
  // source must use it so that no opening or read delays a
  // stop or a seek; a local source may ignore it.
  virtual void set_interrupt(InterruptFn fn, void* ctx) {
    (void)fn;
    (void)ctx;
  }

  virtual bool open() = 0;
  virtual void close() = 0;

  // Reads the next packet. 0 if OK, AVERROR_EOF at end of stream, <0 on error.
  virtual int read(AVPacket* pkt) = 0;

  // Repositions the source to target_ms. Returns false on failure.
  // Called from the demux thread only.
  virtual bool seek(int64_t target_ms) = 0;

  // True if the stream PTS are absolute media positions.
  virtual bool timestamps_absolute() const = 0;

  // True if a read error can be recovered by reopening the source at
  // a position (seek): network drop. A local source cannot.
  virtual bool recoverable() const { return false; }

  // Open ffmpeg context (streams, time_base, codecpar). Valid after open().
  virtual AVFormatContext* format_context() = 0;

  virtual int64_t duration_ms() const = 0;
  virtual int64_t start_time_ms() const = 0;

  // Parameters of the stream of this type currently being read (after open() or a seek, which
  // may reopen another stream: other audio track, other parameters). par is
  // allocated by the caller (avcodec_parameters_alloc). false if no stream.
  // Called from the demux thread.
  virtual bool stream_info(AVMediaType type, AVCodecParameters* par, AVRational* time_base,
                           AVRational* frame_rate);
};

// Fichier local via libavformat.
class FileSource : public MediaSource {
 public:
  explicit FileSource(const char* path) : path_(path) {}
  ~FileSource() override { close(); }

  bool open() override;
  void close() override;
  int read(AVPacket* pkt) override;
  bool seek(int64_t target_ms) override;
  bool timestamps_absolute() const override { return true; }
  AVFormatContext* format_context() override { return ctx_; }
  int64_t duration_ms() const override;
  int64_t start_time_ms() const override;

 private:
  const char* path_;
  AVFormatContext* ctx_ = nullptr;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_MEDIA_SOURCE_H
