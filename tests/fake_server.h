// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_TESTS_FAKE_SERVER_H
#define PELAGIA_TESTS_FAKE_SERVER_H

// Driving of the fake Jellyfin server (tools/fake_jellyfin_server.py) from
// the C++ tests: launch as a subprocess, wait for "READY port=N",
// control requests (/__test/*) and reading of its JSON state.
// Linux test code only (posix_spawn), never linked into the core.

#include <string>
#include <vector>

#include <sys/types.h>

namespace testnet {

// Exit code that makes ctest skip a test (SKIP_RETURN_CODE).
constexpr int kSkip = 77;

// Launches a program, stdout and stderr in output_path; exit code,
// or -1 if the timeout is exceeded (the process is then killed).
int run_process(const std::vector<std::string>& args, const std::string& output_path,
                int timeout_ms);

// Content of a file (empty if unreadable).
std::string read_file(const std::string& path);

// Working directory of the network tests.
std::string workdir();

// Test media (luminance = 16 + 5 x T) in the working directory;
// generated on first call. False if python/ffmpeg are unavailable.
bool ensure_test_media(std::string* path);

class FakeServer {
 public:
  ~FakeServer() { stop(); }

  bool start(const std::string& media, const std::vector<std::string>& extra_args);
  void stop();

  std::string base_url() const;
  int port() const { return port_; }

  // JSON POST on a control route (/__test/fault, /__test/config...).
  bool post(const std::string& path, const std::string& json);
  // JSON body of GET /__test/state (empty on failure).
  std::string state();

 private:
  pid_t pid_ = -1;
  int port_ = 0;
};

// Small accessors to the server's JSON state (through cJSON).
struct StreamRequestInfo {
  long long start_ticks = 0;
  long long actual_start_ticks = 0;
  bool reused = false;
  std::string play_session_id;
  std::string auth;
  // Requested tracks; empty = parameter absent from the URL.
  std::string media_source_id;
  std::string audio_index;
  std::string subtitle_index;
  std::string subtitle_method;
  bool burn = false;  // the server burned in the subtitles
};
struct DeleteInfo {
  std::string play_session_id;
  int status = 0;
  int killed = 0;
};
struct ServerState {
  std::vector<StreamRequestInfo> requests;
  std::vector<DeleteInfo> deletes;
  int active_jobs = 0;
  long long resume_position_ticks = 0;
  bool played = false;
  std::vector<std::string> report_kinds;  // "start", "progress", "stopped"
  std::vector<std::string> report_bodies;  // corps JSON bruts
  std::vector<int> subtitle_downloads;     // index of the requested subtitle tracks
};
bool parse_state(const std::string& json, ServerState* out);

}  // namespace testnet

#endif  // PELAGIA_TESTS_FAKE_SERVER_H
