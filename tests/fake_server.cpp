// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "fake_server.h"

#include <cjson/cJSON.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "api/http_curl.h"

extern char** environ;

namespace testnet {

namespace {

// Paths injected by CMake (tests/CMakeLists.txt).
const char kPython[] = PELAGIA_PYTHON;
const char kScript[] = PELAGIA_FAKE_SERVER_SCRIPT;
const char kWorkdir[] = PELAGIA_FAKE_SERVER_WORKDIR;

pid_t spawn(const std::vector<std::string>& args, int stdout_fd, int stderr_fd = -1) {
  std::vector<char*> argv;
  for (const std::string& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (stdout_fd >= 0) {
    posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
  }
  if (stderr_fd >= 0) {
    posix_spawn_file_actions_adddup2(&actions, stderr_fd, STDERR_FILENO);
  }
  pid_t pid = -1;
  if (posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) != 0) {
    pid = -1;
  }
  posix_spawn_file_actions_destroy(&actions);
  return pid;
}

int wait_exit(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Reads a line on fd in at most timeout_ms.
bool read_line(int fd, std::string* line, int timeout_ms) {
  line->clear();
  while (true) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, timeout_ms) <= 0) {
      return false;
    }
    char c;
    if (read(fd, &c, 1) != 1) {
      return false;
    }
    if (c == '\n') {
      return true;
    }
    line->push_back(c);
  }
}

}  // namespace

std::string workdir() {
  return kWorkdir;
}

int run_process(const std::vector<std::string>& args, const std::string& output_path,
                int timeout_ms) {
  const int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return -1;
  }
  const pid_t pid = spawn(args, fd, fd);
  close(fd);
  if (pid <= 0) {
    return -1;
  }
  for (int waited = 0; waited < timeout_ms; waited += 20) {
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) {
      return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    usleep(20 * 1000);
  }
  kill(pid, SIGKILL);
  wait_exit(pid);
  return -1;
}

std::string read_file(const std::string& path) {
  std::string out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return out;
  }
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  std::fclose(f);
  return out;
}

bool ensure_test_media(std::string* path) {
  mkdir(kWorkdir, 0755);
  *path = std::string(kWorkdir) + "/mire_40s.mp4";
  struct stat st;
  if (stat(path->c_str(), &st) == 0 && st.st_size > 0) {
    return true;
  }
  const pid_t pid =
      spawn({kPython, kScript, "--make-media", *path, "--seconds", "40"}, -1);
  return pid > 0 && wait_exit(pid) == 0;
}

bool FakeServer::start(const std::string& media, const std::vector<std::string>& extra) {
  int fds[2];
  if (pipe(fds) != 0) {
    return false;
  }
  std::vector<std::string> args = {kPython, kScript, "--media", media, "--port", "0"};
  args.insert(args.end(), extra.begin(), extra.end());
  pid_ = spawn(args, fds[1]);
  close(fds[1]);
  std::string line;
  const bool ok = pid_ > 0 && read_line(fds[0], &line, 20000) &&
                  std::sscanf(line.c_str(), "READY port=%d", &port_) == 1;
  close(fds[0]);
  if (!ok) {
    std::fprintf(stderr, "fake server: cannot start (%s)\n", line.c_str());
    stop();
  }
  return ok;
}

void FakeServer::stop() {
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    wait_exit(pid_);
    pid_ = -1;
  }
}

std::string FakeServer::base_url() const {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "http://127.0.0.1:%d", port_);
  return buf;
}

bool FakeServer::post(const std::string& path, const std::string& json) {
  api::HttpCurlTransport transport;
  const api::HttpResponse resp =
      transport.post(base_url() + path, {}, json, "application/json");
  return resp.ok && resp.status >= 200 && resp.status < 300;
}

std::string FakeServer::state() {
  api::HttpCurlTransport transport;
  const api::HttpResponse resp = transport.get(base_url() + "/__test/state", {});
  return resp.ok && resp.status == 200 ? resp.body : std::string();
}

namespace {

std::string json_string(const cJSON* obj, const char* key) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}

long long json_int(const cJSON* obj, const char* key) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsNumber(v) ? static_cast<long long>(v->valuedouble) : 0;
}

}  // namespace

bool parse_state(const std::string& json, ServerState* out) {
  cJSON* root = cJSON_Parse(json.c_str());
  if (!root) {
    return false;
  }
  *out = ServerState{};
  const cJSON* e = nullptr;
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "stream_requests")) {
    StreamRequestInfo r;
    r.start_ticks = json_int(e, "start_ticks");
    r.actual_start_ticks = json_int(e, "actual_start_ticks");
    r.reused = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e, "reused"));
    r.play_session_id = json_string(e, "play_session_id");
    r.auth = json_string(e, "auth");
    r.media_source_id = json_string(e, "media_source_id");
    r.audio_index = json_string(e, "audio_index");
    r.subtitle_index = json_string(e, "subtitle_index");
    r.subtitle_method = json_string(e, "subtitle_method");
    r.burn = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e, "burn"));
    out->requests.push_back(r);
  }
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "deletes")) {
    DeleteInfo d;
    d.play_session_id = json_string(e, "play_session_id");
    d.status = static_cast<int>(json_int(e, "status"));
    d.killed = static_cast<int>(json_int(e, "killed"));
    out->deletes.push_back(d);
  }
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "subtitle_requests")) {
    out->subtitle_downloads.push_back(static_cast<int>(json_int(e, "index")));
  }
  out->active_jobs = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "active_jobs"));
  const cJSON* ud = cJSON_GetObjectItemCaseSensitive(root, "user_data");
  out->resume_position_ticks = json_int(ud, "PlaybackPositionTicks");
  out->played = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ud, "Played"));
  cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "reports")) {
    out->report_kinds.push_back(json_string(e, "kind"));
    char* body = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(e, "body"));
    out->report_bodies.push_back(body ? body : "");
    cJSON_free(body);
  }
  cJSON_Delete(root);
  return true;
}

}  // namespace testnet
