// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_DECODER_H
#define PELAGIA_CORE_PLAYER_DECODER_H

// Video and audio decoders (libavcodec). Used exclusively by the
// player's decoding threads.
// - Video: YUV420p output (swscale conversion only if the source
//   is not already YUV420p). No RGB conversion in the core.
// - Audio: interleaved S16 output, rate/channels of platform::AudioSpec
//   (48 kHz stereo by default), through swresample.

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace player {

class VideoDecoder {
 public:
  ~VideoDecoder() { close(); }

  bool open(const AVStream* stream);
  // Opening from the parameters of a stream (reopening another stream
  // after a track change, with no AVStream at hand).
  bool open(const AVCodecParameters* par, AVRational time_base, AVRational frame_rate);
  void close();

  // Sends a packet (nullptr = end-of-stream drain).
  int send(const AVPacket* pkt);

  // Gets the next decoded frame, converted to YUV420p if needed.
  // Returns nullptr with *status = AVERROR(EAGAIN) (next packet required)
  // or AVERROR_EOF (drain finished). The frame belongs to the caller
  // (av_frame_free).
  AVFrame* receive(int* status);

  void flush();  // after a seek

  // PTS of the frame in milliseconds (best_effort, otherwise extrapolation).
  int64_t frame_pts_ms(const AVFrame* frame);

  // Total time spent in decoding/conversion, in ms (headless stats).
  double busy_ms() const { return busy_ms_; }

  // Nominal duration of a frame (1 / average frame rate of the stream).
  int64_t frame_duration_ms() const { return frame_duration_ms_; }

 private:
  AVCodecContext* codec_ = nullptr;
  SwsContext* sws_ = nullptr;
  AVRational time_base_{1, 1000};
  int64_t last_pts_ms_ = 0;
  int64_t frame_duration_ms_ = 0;
  double busy_ms_ = 0.0;
};

// Decoded and converted audio frame: owned interleaved S16 buffer (av_free).
struct AudioChunk {
  uint8_t* data = nullptr;
  size_t bytes = 0;
  int64_t pts_ms = 0;
  int64_t duration_ms = 0;
};

class AudioDecoder {
 public:
  ~AudioDecoder() { close(); }

  bool open(const AVStream* stream, int out_sample_rate, int out_channels);
  bool open(const AVCodecParameters* par, AVRational time_base, int out_sample_rate,
            int out_channels);
  void close();

  int send(const AVPacket* pkt);

  // Gets the next converted chunk. false with *status = EAGAIN/EOF.
  // out->data belongs to the caller (av_free).
  bool receive(AudioChunk* out, int* status);

  void flush();

  int out_sample_rate() const { return out_sample_rate_; }
  int out_channels() const { return out_channels_; }
  double busy_ms() const { return busy_ms_; }

 private:
  bool convert_frame(const AVFrame* frame, AudioChunk* out);

  AVCodecContext* codec_ = nullptr;
  SwrContext* swr_ = nullptr;
  AVRational time_base_{1, 1000};
  int out_sample_rate_ = 48000;
  int out_channels_ = 2;
  int64_t next_pts_ms_ = 0;
  double busy_ms_ = 0.0;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_DECODER_H
