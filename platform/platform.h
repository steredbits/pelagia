// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_PLATFORM_H
#define PELAGIA_PLATFORM_H

// Abstract interface of the platform layer.
// /core never includes a header from /platform/linux or /platform/ps5:
// only this file. Each platform provides the implementation.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// --- Life cycle -------------------------------------------------------------

struct Config {
    int window_width = 1920;
    int window_height = 1080;
    const char* window_title = "Pelagia";
    bool fullscreen = false;  // full screen (on PS5: always)
};

// Logical drawing size: all rendering coordinates are expressed
// in 1920x1080, whatever the real output (resized window,
// 4K TV); the platform scales with linear filtering.
constexpr int kLogicalWidth = 1920;
constexpr int kLogicalHeight = 1080;

// Returns false on failure (no exceptions in the project).
bool init(const Config& config);
void shutdown();
// Called at the very end of main(), after shutdown(), with the exit code.
// Linux: does nothing (main returns: static destructors, sanitizers).
// PS5: flushes the logs and ends the process right away, without static
// destructors or atexit handlers (see platform/ps5/platform_ps5.cpp).
void finish_process(int status);

// --- Rendu ------------------------------------------------------------------

// Start of frame: clears the render surface (black).
void render_begin();
// End of frame: presents on screen everything drawn since
// render_begin(), in call order.
void render_present();

// --- Dessin 2D (interface) -------------------------------------------------
// Minimal primitives: the UI (core/ui) builds everything else (text, posters,
// bars) from filled rectangles and textures.

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Non-premultiplied color; a = 255 opaque.
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

// Rgba8: 4 bytes per pixel in R, G, B, A order (non-premultiplied alpha).
// Alpha8: 1 coverage byte per pixel (glyphs, icons), drawn in the
// tint color.
enum class PixelFormat : uint8_t { Rgba8, Alpha8 };

using TextureId = uint32_t;  // 0 = invalide

// Textures: 0 on failure. Initial content undefined.
TextureId texture_create(int w, int h, PixelFormat format);
// Replaces the area (x, y, w, h); pitch = bytes per pixel row.
bool texture_update(TextureId id, int x, int y, int w, int h, const void* pixels,
                    int pitch);
void texture_destroy(TextureId id);

// Filled rectangle, blended according to color.a.
void draw_rect(const Rect& rect, Color color);
// Copies the src area of the texture to dst (linear scaling if the
// sizes differ). tint multiplies the color (Rgba8) or gives it (Alpha8);
// tint.a modulates the opacity.
void draw_texture(TextureId id, const Rect& src, const Rect& dst, Color tint);
// Clipping: nullptr removes it.
void set_clip(const Rect* clip);

// Draws the last frame submitted by video_submit_frame_yuv(), aspect ratio
// kept (bars left as they are) and centered in area. Without a frame,
// draws nothing.
void draw_video(const Rect& area);
// Forgets the last frame (end of playback): draw_video no longer draws anything.
void video_clear();

// Submits a YUV420p video frame (3 planes + strides). The frame is copied
// to the internal texture; it is drawn by draw_video().
// Returns false on failure (invalid size, texture unavailable).
bool video_submit_frame_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                            int stride_y, int stride_u, int stride_v,
                            int w, int h);

// --- Audio ------------------------------------------------------------------

struct AudioSpec {
    int sample_rate = 48000;
    int channels = 2;  // interleaved S16 PCM
};

bool audio_open(const AudioSpec& spec);
void audio_close();
// Submits PCM samples; returns the number of bytes accepted.
size_t audio_queue(const void* samples, size_t bytes);
// Bytes submitted but not yet played by the device.
// Needed by the player's audio clock (position = submitted - pending).
size_t audio_queued_bytes();
// Pauses / resumes audio output (playback pause).
void audio_pause(bool paused);
// Drops audio submitted but not yet played (seek).
void audio_flush();

// --- Input ------------------------------------------------------------------

// Abstract events: the gamepad (or the keyboard on Linux) is mapped
// to these values by the platform layer. The meaning depends on the screen: in
// the player, Left/Right are short seeks; elsewhere, navigation.
enum class InputEvent : uint8_t {
    None,
    Quit,          // application closing (Ctrl+Q, window closed)
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    PlayPause,
    SeekFwd,       // seek court (+10 s) : R1
    SeekBack,      // seek court (-10 s) : L1
    SeekFwdLong,   // seek long (+60 s) : R2
    SeekBackLong,  // seek long (-60 s) : L2
    Menu,          // options contextuelles : bouton Options
    Erase,         // erase a character (Backspace while typing, square)
    Text,          // character typed on the physical keyboard (Input::codepoint)
};

struct Input {
    InputEvent event = InputEvent::None;
    uint32_t codepoint = 0;  // set for InputEvent::Text
};

// Pops the next event (event == None if there is none). Held
// directions are repeated by the platform (fast navigation).
Input poll_input();

// Typing mode: active when a text field has focus. The physical keyboard
// then produces Text events for all printable keys (Q and
// Space included) and Erase for Backspace; outside typing, these keys
// are commands. Ctrl+Q quits in both modes.
void set_text_input(bool enabled);

// --- System language ----------------------------------------------------------

// Languages preferred by the system, most preferred first, as "xx" or "xx_YY"
// (e.g. "fr_FR"). Empty when the system gives none: the application then
// uses English. Linux: from the environment (LANG...); PS5: see platform_sdl.cpp.
std::vector<std::string> preferred_locales();

// --- Fichiers ---------------------------------------------------------------
// Persistent configuration and cache. Absolute paths, '/' separator.

// Application folders (not created by these functions): configuration
// (server address, token) and cache (posters).
std::string config_dir();
std::string cache_dir();
// Optional pre-configuration files (server=..., see util/preconfig.h),
// by priority: full paths, the files may not exist.
// They are never written: session, cache and logs stay in config_dir()
// and cache_dir().
std::vector<std::string> preconfig_files();

bool make_dirs(const std::string& path);
bool file_read(const std::string& path, std::string* out);
// Atomic write (temporary file then rename). private_file: file
// readable by the owner only (0600 on Linux), for the token.
bool file_write(const std::string& path, const std::string& data, bool private_file);
bool file_remove(const std::string& path);
// Updates the last-modification date (LRU order of the disk cache).
bool file_touch(const std::string& path);

struct FileInfo {
    std::string name;   // name only, without the folder
    uint64_t size = 0;
    int64_t mtime_s = 0;
};
// Regular files of the folder (no sub-folders, no "." / "..").
bool list_dir(const std::string& path, std::vector<FileInfo>* out);

// --- Temps ------------------------------------------------------------------

// Monotonic clock in milliseconds since an arbitrary instant.
uint64_t ticks_ms();
void sleep_ms(uint32_t ms);

}  // namespace platform

#endif  // PELAGIA_PLATFORM_H
