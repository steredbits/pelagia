// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the asynchronous tasks and the poster cache (memory + disk), without
// network or display: manual task execution, software canvas.

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "memory_file_store.h"
#include "test_framework.h"
#include "ui/image_codec.h"
#include "ui/poster_cache.h"
#include "ui/soft_canvas.h"
#include "ui/tasks.h"

using ui::PosterCache;
using ui::PosterKey;

namespace {

std::string make_png(int w, int h) {
  ui::Image img;
  img.w = w;
  img.h = h;
  img.rgba.assign(static_cast<size_t>(w) * h * 4, 200);
  std::string png;
  ui::encode_png(img, &png);
  return png;
}

struct FakeFetcher : ui::PosterFetcher {
  std::atomic<int> calls{0};
  bool fail = false;
  bool garbage = false;
  bool fetch(const PosterKey& key, std::string* bytes) override {
    ++calls;
    if (fail) return false;
    *bytes = garbage ? std::string("<html>") : make_png(key.width * 2, key.width * 3);
    return true;
  }
};

PosterKey key(const char* id, int width = 40) {
  PosterKey k;
  k.item_id = id;
  k.tag = "tag1";
  k.width = width;
  return k;
}

struct Fixture {
  ui::SoftCanvas canvas{8, 8};
  ui::ManualRunner io;
  ui::MainQueue main;
  FakeFetcher fetcher;
  MemoryFileStore fs;
  util::DiskCache disk{&fs, "/cache", 1u << 20};
  uint64_t now = 0;

  Fixture() { disk.init(); }
  // One frame: tasks executed, results applied, textures created.
  void step(PosterCache& cache) {
    io.run_all();
    main.drain();
    cache.begin_frame(now += 16);
  }
};

}  // namespace

static void test_tasks() {
  ui::MainQueue main;
  int applied = 0;
  {
    ui::ThreadPool pool(2, "test");
    std::atomic<int> done{0};
    for (int i = 0; i < 20; ++i) {
      pool.post([&] {
        ++done;
        main.post([&] { ++applied; });
      });
    }
    for (int i = 0; i < 200 && done < 20; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_EQ(done.load(), 20);
  }
  CHECK_EQ(applied, 0);  // nothing is applied outside the main thread
  CHECK_EQ(main.drain(), 20u);
  CHECK_EQ(applied, 20);

  // Lifetime token: expired when its owner is destroyed.
  ui::Lifetime::Token token;
  {
    ui::Lifetime life;
    token = life.token();
    CHECK(ui::Lifetime::alive(token));
  }
  CHECK(!ui::Lifetime::alive(token));
  CHECK(!ui::Lifetime::alive(ui::Lifetime::Token()));
}

static void test_load_and_memory_hit() {
  Fixture f;
  PosterCache cache(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, PosterCache::Options());
  CHECK(cache.get(key("a")) == nullptr);  // not ready: loading started
  CHECK(cache.busy());
  CHECK_EQ(f.io.pending(), 1u);
  CHECK(cache.get(key("a")) == nullptr);  // no double request
  CHECK_EQ(f.io.pending(), 1u);
  f.step(cache);
  const ui::Poster* p = cache.get(key("a"));
  CHECK(p != nullptr);
  if (p) {
    CHECK(p->texture != 0);
    CHECK(p->w == 40 && p->h == 60);  // scaled down to the requested width
  }
  CHECK(!cache.busy());
  CHECK_EQ(f.fetcher.calls.load(), 1);
  CHECK_EQ(cache.memory_bytes(), 40u * 60 * 4);
  CHECK_EQ(f.disk.entry_count(), 1u);  // kept on disk
  // Without a poster on the server side (empty tag): nothing is requested.
  PosterKey none = key("b");
  none.tag.clear();
  CHECK(cache.get(none) == nullptr);
  CHECK_EQ(f.io.pending(), 0u);
}

static void test_disk_hit() {
  Fixture f;
  {
    PosterCache first(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, PosterCache::Options());
    first.get(key("a"));
    f.step(first);
  }
  CHECK_EQ(f.canvas.texture_count(), 0u);  // textures released at destruction
  PosterCache second(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, PosterCache::Options());
  second.get(key("a"));
  f.step(second);
  CHECK(second.get(key("a")) != nullptr);
  CHECK_EQ(f.fetcher.calls.load(), 1);  // read from disk, not downloaded again
}

static void test_failure_and_retry() {
  Fixture f;
  f.fetcher.fail = true;
  PosterCache::Options opt;
  opt.retry_failed_ms = 1000;
  PosterCache cache(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, opt);
  cache.get(key("a"));
  f.step(cache);
  CHECK(cache.get(key("a")) == nullptr);
  CHECK(!cache.busy());
  f.step(cache);
  CHECK_EQ(f.fetcher.calls.load(), 1);  // no request loop
  f.fetcher.fail = false;
  f.now += 2000;
  f.step(cache);
  cache.get(key("a"));  // delay elapsed: new attempt
  f.step(cache);
  CHECK(cache.get(key("a")) != nullptr);
  CHECK_EQ(f.fetcher.calls.load(), 2);
  // Response that is not an image: clean failure, nothing on disk.
  f.fetcher.garbage = true;
  cache.get(key("z"));
  f.step(cache);
  CHECK(cache.get(key("z")) == nullptr);
  CHECK_EQ(f.disk.entry_count(), 1u);
}

static void test_abandon_unwanted() {
  Fixture f;
  PosterCache::Options opt;
  opt.unwanted_after_frames = 3;
  PosterCache cache(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, opt);
  cache.get(key("a"));  // requested once (fast scrolling)...
  for (int i = 0; i < 5; ++i) {
    cache.begin_frame(f.now += 16);  // ... then never again, task not yet started
  }
  CHECK(!cache.busy());
  f.io.run_all();
  f.main.drain();
  CHECK_EQ(f.fetcher.calls.load(), 0);  // abandoned before the download
  cache.get(key("a"));  // redevient visible : nouvelle demande
  f.step(cache);
  CHECK(cache.get(key("a")) != nullptr);
}

static void test_upload_rate_and_eviction() {
  Fixture f;
  PosterCache::Options opt;
  opt.uploads_per_frame = 2;
  opt.memory_budget = 3 * 40 * 60 * 4;  // trois affiches
  PosterCache cache(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, opt);
  const char* ids[] = {"a", "b", "c", "d", "e"};
  for (const char* id : ids) cache.get(key(id));
  f.io.run_all();
  f.main.drain();
  cache.begin_frame(f.now += 16);
  CHECK_EQ(cache.texture_count(), 2u);  // uploads spread over several frames
  CHECK(cache.busy());
  for (const char* id : ids) cache.get(key(id));  // all visible
  cache.begin_frame(f.now += 16);
  for (const char* id : ids) cache.get(key(id));
  cache.begin_frame(f.now += 16);
  CHECK_EQ(cache.texture_count(), 5u);  // visible: overrun tolerated
  // Only d and e remain displayed: the oldest are evicted.
  for (int i = 0; i < 2; ++i) {
    cache.get(key("d"));
    cache.get(key("e"));
    cache.begin_frame(f.now += 16);
  }
  CHECK(cache.memory_bytes() <= opt.memory_budget);
  CHECK(cache.get(key("d")) != nullptr);
  CHECK(cache.get(key("e")) != nullptr);
  CHECK_EQ(f.canvas.texture_count(), cache.texture_count());
}

static void test_destroyed_before_result() {
  Fixture f;
  {
    PosterCache cache(&f.canvas, &f.io, &f.main, &f.fetcher, &f.disk, PosterCache::Options());
    cache.get(key("a"));
    f.io.run_all();  // result posted...
  }
  f.main.drain();  // ... after the cache was destroyed: ignored without a crash
  CHECK_EQ(f.canvas.texture_count(), 0u);
}

int main() {
  test_tasks();
  test_load_and_memory_hit();
  test_disk_hit();
  test_failure_and_retry();
  test_abandon_unwanted();
  test_upload_rate_and_eviction();
  test_destroyed_before_result();
  return testfw::test_failures();
}
