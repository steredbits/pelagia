// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// SDL2 platform layer shared by Linux and the PS5 (rendering, audio, input,
// time). Differences go through sdl_hooks.h (platform/linux,
// platform/ps5); files are in platform/posix.
// Also works without a display or sound card with the dummy drivers:
//   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
// On PS5, the renderer is "software" (CPU display): validated on the console,
// IYUV 1080p texture at 60 fps for ~26% of one core.

#include "platform.h"

#include "controller_set.h"
#include "input_map.h"
#include "letterbox.h"
#include "sdl_hooks.h"
#include "text_input.h"
#include "util/log.h"

#include <SDL.h>

#include <chrono>
#include <deque>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>

namespace platform {

namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
int g_texture_w = 0;
int g_texture_h = 0;
SDL_AudioDeviceID g_audio_device = 0;
platform_sdl::ControllerSet<SDL_GameController*> g_controllers;

struct TextureEntry {
  SDL_Texture* texture = nullptr;
  PixelFormat format = PixelFormat::Rgba8;
  int w = 0;
  int h = 0;
};
std::unordered_map<TextureId, TextureEntry> g_textures;
TextureId g_next_texture = 1;
std::vector<uint8_t> g_convert;  // Alpha8 -> RGBA (SDL2 has no A8 texture)

bool g_text_mode = false;
std::deque<Input> g_pending;
platform_sdl::RepeatTracker g_repeat;
// Axes: triggers (long seeks) and left stick (directions).
platform_sdl::AxisEdge g_trigger_left;
platform_sdl::AxisEdge g_trigger_right;
platform_sdl::AxisEdge g_stick_x;
platform_sdl::AxisEdge g_stick_y;

void push(InputEvent ev, uint32_t codepoint = 0) {
  if (ev == InputEvent::None) {
    return;
  }
  Input in;
  in.event = ev;
  in.codepoint = codepoint;
  g_pending.push_back(in);
}

// Opens the gamepad with SDL index `index`, unless it is already open.
void open_controller(int index) {
  if (g_controllers.contains(SDL_JoystickGetDeviceInstanceID(index))) {
    return;
  }
  if (!SDL_IsGameController(index)) {
    LOG_WARN("Joystick ignored (no SDL gamepad mapping): %s",
             SDL_JoystickNameForIndex(index));
    return;
  }
  SDL_GameController* pad = SDL_GameControllerOpen(index);
  if (!pad) {
    LOG_WARN("Cannot open the gamepad: %s", SDL_GetError());
    return;
  }
  const SDL_GameControllerType type = SDL_GameControllerGetType(pad);
  const bool playstation = type == SDL_CONTROLLER_TYPE_PS3 ||
                           type == SDL_CONTROLLER_TYPE_PS4 || type == SDL_CONTROLLER_TYPE_PS5;
  const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
  g_controllers.add(id, pad, playstation);
  LOG_INFO("Manette n°%d : %s (disposition %s), %zu ouverte(s)", static_cast<int>(id),
           SDL_GameControllerName(pad), playstation ? "PlayStation" : "standard",
           g_controllers.size());
}

void close_controller(SDL_JoystickID id) {
  SDL_GameController* pad = nullptr;
  if (g_controllers.remove(id, &pad)) {
    SDL_GameControllerClose(pad);
    g_repeat.release(g_repeat.held());
    LOG_INFO("Gamepad no. %d removed, %zu open", static_cast<int>(id), g_controllers.size());
  }
}

// SDL internal messages: through util/log like the rest (usable PS5 logs).
void sdl_log_output(void*, int, SDL_LogPriority priority, const char* message) {
  const util::LogLevel level = priority >= SDL_LOG_PRIORITY_ERROR  ? util::LogLevel::Error
                               : priority >= SDL_LOG_PRIORITY_WARN ? util::LogLevel::Warn
                               : priority >= SDL_LOG_PRIORITY_INFO ? util::LogLevel::Info
                                                                   : util::LogLevel::Debug;
  util::log_write(level, "SDL : %s", message);
}

// Direction of a stick: press/release like the d-pad (repetition included).
void stick_direction(int fired, int state, InputEvent negative, InputEvent positive) {
  if (fired != 0) {
    const InputEvent ev = fired > 0 ? positive : negative;
    push(ev);
    g_repeat.press(ev, ticks_ms());
  } else if (state == 0) {
    g_repeat.release(negative);
    g_repeat.release(positive);
  }
}

void handle_axis(const SDL_ControllerAxisEvent& axis) {
  switch (axis.axis) {
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
      if (g_trigger_left.update(axis.value) > 0) push(InputEvent::SeekBackLong);
      break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
      if (g_trigger_right.update(axis.value) > 0) push(InputEvent::SeekFwdLong);
      break;
    case SDL_CONTROLLER_AXIS_LEFTX: {
      const int fired = g_stick_x.update(axis.value);
      stick_direction(fired, g_stick_x.state(), InputEvent::Left, InputEvent::Right);
      break;
    }
    case SDL_CONTROLLER_AXIS_LEFTY: {
      const int fired = g_stick_y.update(axis.value);
      stick_direction(fired, g_stick_y.state(), InputEvent::Up, InputEvent::Down);
      break;
    }
    default:
      break;
  }
}

void handle_text(const char* text) {
  const std::string s(text);
  for (size_t pos = 0; pos < s.size();) {
    // Minimal UTF-8 decoding (SDL provides valid UTF-8).
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    if (pos + len > s.size()) {
      break;
    }
    uint32_t cp = len == 1 ? c : (c & (0x7F >> len));
    for (int k = 1; k < len; ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(s[pos + k]) & 0x3F);
    }
    pos += len;
    if (cp >= 0x20 && cp != 0x7F) {
      push(InputEvent::Text, cp);
    }
  }
}

void handle_event(const SDL_Event& event) {
  switch (event.type) {
    case SDL_QUIT:
      push(InputEvent::Quit);
      break;
    case SDL_KEYDOWN: {
      const bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
      push(platform_sdl::map_key(event.key.keysym.sym, ctrl, g_text_mode,
                                 event.key.repeat != 0));
      break;
    }
    case SDL_TEXTINPUT:
      if (g_text_mode) {
        handle_text(event.text.text);
      }
      break;
    case SDL_CONTROLLERBUTTONDOWN: {
      const InputEvent ev = platform_sdl::map_button(
          event.cbutton.button, g_controllers.playstation(event.cbutton.which));
      push(ev);
      if (platform_sdl::is_direction(ev)) {
        g_repeat.press(ev, ticks_ms());
      }
      break;
    }
    case SDL_CONTROLLERBUTTONUP:
      g_repeat.release(platform_sdl::map_button(event.cbutton.button,
                                                g_controllers.playstation(event.cbutton.which)));
      break;
    case SDL_CONTROLLERAXISMOTION:
      handle_axis(event.caxis);
      break;
    case SDL_CONTROLLERDEVICEADDED:
      open_controller(event.cdevice.which);
      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      close_controller(event.cdevice.which);
      break;
    default:
      break;
  }
}

}  // namespace

bool init(const Config& requested) {
  platform_sdl::before_init();
  SDL_LogSetOutputFunction(&sdl_log_output, nullptr);
  const Config config = platform_sdl::adjust_config(requested);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  g_window = SDL_CreateWindow(config.window_title, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, config.window_width,
                              config.window_height,
                              SDL_WINDOW_RESIZABLE |
                                  (config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
  if (!g_window) {
    LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  // Linear filtering of all textures (posters, scaling from
  // the logical size to the window).
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (!g_renderer) {
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!g_renderer) {
    LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
    return false;
  }
  SDL_RendererInfo info{};
  SDL_GetRendererInfo(g_renderer, &info);
  LOG_INFO("SDL platform: video driver %s, renderer %s", SDL_GetCurrentVideoDriver(),
           info.name);
  // Logical coordinates 1920x1080 whatever the window size.
  SDL_RenderSetLogicalSize(g_renderer, kLogicalWidth, kLogicalHeight);
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  // All gamepads present (see controller_set.h).
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    open_controller(i);
  }
  // No text input as long as no field has focus.
  SDL_StopTextInput();
  return true;
}

// Each step is logged: on PS5, the last "Shutdown: ..." line of the
// log shows the step where a possible crash happened.
void shutdown() {
  LOG_INFO("Shutdown: SDL platform, %zu gamepad(s), %zu remaining texture(s)",
           g_controllers.size(), g_textures.size());
  for (const auto& entry : g_controllers.entries()) {
    SDL_GameControllerClose(entry.handle);
  }
  g_controllers.clear();
  LOG_INFO("Shutdown: gamepads closed");
  video_clear();
  for (auto& entry : g_textures) {
    SDL_DestroyTexture(entry.second.texture);
  }
  g_textures.clear();
  LOG_INFO("Shutdown: textures destroyed");
  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  LOG_INFO("Shutdown: renderer destroyed");
  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  LOG_INFO("Shutdown: window destroyed");
  audio_close();
  LOG_INFO("Shutdown: SDL_Quit");
  SDL_Quit();
  LOG_INFO("Shutdown: SDL_Quit done");
}

void finish_process(int status) { platform_sdl::finish_process(status); }

void render_begin() {
  if (!g_renderer) {
    return;
  }
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
  SDL_RenderClear(g_renderer);
}

void render_present() {
  if (g_renderer) {
    SDL_RenderPresent(g_renderer);
  }
}

TextureId texture_create(int w, int h, PixelFormat format) {
  if (!g_renderer || w <= 0 || h <= 0) {
    return 0;
  }
  SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC, w, h);
  if (!texture) {
    LOG_ERROR("SDL_CreateTexture(%dx%d) failed: %s", w, h, SDL_GetError());
    return 0;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
  const TextureId id = g_next_texture++;
  TextureEntry entry;
  entry.texture = texture;
  entry.format = format;
  entry.w = w;
  entry.h = h;
  g_textures[id] = entry;
  return id;
}

bool texture_update(TextureId id, int x, int y, int w, int h, const void* pixels,
                    int pitch) {
  auto it = g_textures.find(id);
  if (it == g_textures.end() || !pixels || w <= 0 || h <= 0) {
    return false;
  }
  const SDL_Rect area{x, y, w, h};
  if (it->second.format == PixelFormat::Rgba8) {
    return SDL_UpdateTexture(it->second.texture, &area, pixels, pitch) == 0;
  }
  // Alpha8: white + coverage as alpha; the tint comes from the color mod.
  g_convert.resize(static_cast<size_t>(w) * h * 4);
  const uint8_t* src = static_cast<const uint8_t*>(pixels);
  for (int row = 0; row < h; ++row) {
    uint8_t* dst = g_convert.data() + static_cast<size_t>(row) * w * 4;
    const uint8_t* line = src + static_cast<size_t>(row) * pitch;
    for (int col = 0; col < w; ++col) {
      dst[col * 4 + 0] = 255;
      dst[col * 4 + 1] = 255;
      dst[col * 4 + 2] = 255;
      dst[col * 4 + 3] = line[col];
    }
  }
  return SDL_UpdateTexture(it->second.texture, &area, g_convert.data(), w * 4) == 0;
}

void texture_destroy(TextureId id) {
  auto it = g_textures.find(id);
  if (it != g_textures.end()) {
    SDL_DestroyTexture(it->second.texture);
    g_textures.erase(it);
  }
}

void draw_rect(const Rect& rect, Color color) {
  if (!g_renderer || rect.w <= 0 || rect.h <= 0) {
    return;
  }
  SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
  const SDL_Rect r{rect.x, rect.y, rect.w, rect.h};
  SDL_RenderFillRect(g_renderer, &r);
}

void draw_texture(TextureId id, const Rect& src, const Rect& dst, Color tint) {
  auto it = g_textures.find(id);
  if (!g_renderer || it == g_textures.end() || dst.w <= 0 || dst.h <= 0) {
    return;
  }
  SDL_SetTextureColorMod(it->second.texture, tint.r, tint.g, tint.b);
  SDL_SetTextureAlphaMod(it->second.texture, tint.a);
  const SDL_Rect s{src.x, src.y, src.w, src.h};
  const SDL_Rect d{dst.x, dst.y, dst.w, dst.h};
  SDL_RenderCopy(g_renderer, it->second.texture, &s, &d);
}

void set_clip(const Rect* clip) {
  if (!g_renderer) {
    return;
  }
  if (!clip) {
    SDL_RenderSetClipRect(g_renderer, nullptr);
    return;
  }
  const SDL_Rect r{clip->x, clip->y, clip->w, clip->h};
  SDL_RenderSetClipRect(g_renderer, &r);
}

void draw_video(const Rect& area) {
  if (!g_renderer || !g_texture) {
    return;
  }
  // Aspect ratio kept, bars left to the background (black after render_begin);
  // recomputed at each frame to follow resizing.
  const platform_sdl::FitRect fit =
      platform_sdl::fit_rect(g_texture_w, g_texture_h, area.w, area.h);
  const SDL_Rect dst{area.x + fit.x, area.y + fit.y, fit.w, fit.h};
  SDL_RenderCopy(g_renderer, g_texture, nullptr, &dst);
}

void video_clear() {
  if (g_texture) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
    g_texture_w = 0;
    g_texture_h = 0;
  }
}

bool video_submit_frame_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                            int stride_y, int stride_u, int stride_v,
                            int w, int h) {
  if (!g_renderer || !y || !u || !v || w <= 0 || h <= 0) {
    return false;
  }
  if (!g_texture || g_texture_w != w || g_texture_h != h) {
    if (g_texture) {
      SDL_DestroyTexture(g_texture);
    }
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_IYUV,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_texture) {
      LOG_ERROR("SDL_CreateTexture (video %dx%d) failed: %s", w, h, SDL_GetError());
      return false;
    }
    // Linear filtering: without it SDL scales with nearest neighbor
    // (grainy image when the window is smaller than the video).
    if (SDL_SetTextureScaleMode(g_texture, SDL_ScaleModeLinear) != 0) {
      LOG_WARN("SDL_SetTextureScaleMode(linear) failed: %s", SDL_GetError());
    }
    g_texture_w = w;
    g_texture_h = h;
  }
  return SDL_UpdateYUVTexture(g_texture, nullptr, y, stride_y, u, stride_u, v,
                              stride_v) == 0;
}

bool audio_open(const AudioSpec& spec) {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    LOG_ERROR("SDL_InitSubSystem(audio) failed: %s", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want{};
  want.freq = spec.sample_rate;
  want.format = AUDIO_S16SYS;
  want.channels = static_cast<Uint8>(spec.channels);
  want.samples = 1024;
  g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
  if (g_audio_device == 0) {
    LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return false;
  }
  SDL_PauseAudioDevice(g_audio_device, 0);
  return true;
}

void audio_close() {
  if (g_audio_device != 0) {
    SDL_CloseAudioDevice(g_audio_device);
    g_audio_device = 0;
  }
}

size_t audio_queue(const void* samples, size_t bytes) {
  if (g_audio_device == 0) {
    return 0;
  }
  if (SDL_QueueAudio(g_audio_device, samples, static_cast<Uint32>(bytes)) != 0) {
    return 0;
  }
  return bytes;
}

size_t audio_queued_bytes() {
  if (g_audio_device == 0) {
    return 0;
  }
  return SDL_GetQueuedAudioSize(g_audio_device);
}

void audio_flush() {
  if (g_audio_device != 0) {
    SDL_ClearQueuedAudio(g_audio_device);
  }
}

void audio_pause(bool paused) {
  if (g_audio_device != 0) {
    SDL_PauseAudioDevice(g_audio_device, paused ? 1 : 0);
  }
}

Input poll_input() {
  if (g_pending.empty()) {
    SDL_Event event;
    while (g_pending.empty() && SDL_PollEvent(&event)) {
      handle_event(event);
    }
  }
  if (g_pending.empty()) {
    push(g_repeat.poll(ticks_ms()));
  }
  if (g_pending.empty()) {
    return Input{};
  }
  const Input in = g_pending.front();
  g_pending.pop_front();
  return in;
}

void set_text_input(bool enabled) {
  if (enabled == g_text_mode) {
    return;
  }
  g_text_mode = enabled;
  // PS5: never SDL's IME, the application's on-screen keyboard is enough.
  if (!platform_sdl::should_start_sdl_text_input(enabled, platform_sdl::system_text_input())) {
    return;
  }
  if (enabled) {
    SDL_StartTextInput();
  } else {
    SDL_StopTextInput();
  }
}

std::vector<std::string> preferred_locales() {
  std::vector<std::string> out;
  // SDL >= 2.0.14. Nothing more is attempted when it fails or returns an empty list.
  SDL_Locale* locales = SDL_GetPreferredLocales();
  if (!locales) {
    return out;
  }
  for (const SDL_Locale* l = locales; l->language; ++l) {
    std::string code = l->language;
    if (l->country && l->country[0] != '\0') {
      code += "_";
      code += l->country;
    }
    out.push_back(std::move(code));
  }
  SDL_free(locales);
  return out;
}

uint64_t ticks_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void sleep_ms(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace platform
