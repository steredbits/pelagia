# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Core library: portable code, no platform dependency.
# libcurl, cJSON and ffmpeg are allowed here (also available on the PS5 side,
# see docs/ARCHITECTURE.md). curl and ffmpeg through pkg-config on both platforms (on
# PS5 : prospero-pkg-config, liens statiques PacBrew) ; cJSON : third_party.

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JPS5_CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libswscale libswresample libavutil)

# Embedded fonts (assets/fonts): C arrays generated at build time.
set(PELAGIA_FONT_REGULAR ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf)
set(PELAGIA_FONT_BOLD ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Bold.ttf)
set(PELAGIA_FONTS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_fonts.cpp)
add_custom_command(
    OUTPUT ${PELAGIA_FONTS_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -DOUT=${PELAGIA_FONTS_CPP}
            -DREGULAR=${PELAGIA_FONT_REGULAR} -DBOLD=${PELAGIA_FONT_BOLD}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
    DEPENDS ${PELAGIA_FONT_REGULAR} ${PELAGIA_FONT_BOLD}
            ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
    COMMENT "Embedded fonts"
    VERBATIM)

# Logs: separate library, also used by the platform layer (SDL and
# platform messages go through util/log).
add_library(pelagia_log STATIC util/log.cpp util/log_file.cpp)
target_include_directories(pelagia_log PUBLIC ${CMAKE_SOURCE_DIR}/core)

# util/version.h: project version (project(... VERSION) of the root CMakeLists).
configure_file(util/version.h.in ${CMAKE_BINARY_DIR}/generated/util/version.h @ONLY)

add_library(pelagia_core STATIC
    util/session_id.cpp
    util/text.cpp
    util/languages.cpp
    util/i18n.cpp
    util/subtitles.cpp
    util/preconfig.cpp
    util/disk_cache.cpp
    api/http_curl.cpp
    api/jellyfin_json.cpp
    api/jellyfin_urls.cpp
    api/jellyfin_client.cpp
    api/jellyfin_stream.cpp
    api/track_selection.cpp
    api/track_labels.cpp
    api/jellyfin_reporter.cpp
    player/clock.cpp
    player/av_sync.cpp
    player/av_log_bridge.cpp
    player/buffering.cpp
    player/retry_policy.cpp
    player/playback_monitor.cpp
    player/seek_offset.cpp
    player/media_source.cpp
    player/network_source.cpp
    player/decoder.cpp
    player/player.cpp
    ui/soft_canvas.cpp
    ui/image_codec.cpp
    ui/font.cpp
    ui/text_renderer.cpp
    ui/tasks.cpp
    ui/poster_cache.cpp
    ui/format.cpp
    ui/session_store.cpp
    ui/settings_store.cpp
    ui/backend.cpp
    ui/jellyfin_backend.cpp
    ui/focus.cpp
    ui/track_menu.cpp
    ui/painter.cpp
    ui/virtual_keyboard.cpp
    ui/app.cpp
    ui/screens/screens.cpp
    ui/screens/about_screen.cpp
    ui/screens/login_screen.cpp
    ui/screens/splash_screen.cpp
    ui/screens/home_screen.cpp
    ui/screens/grid_screen.cpp
    ui/screens/detail_screen.cpp
    ui/screens/series_screen.cpp
    ui/screens/player_screen.cpp
    ui/playback_session.cpp
    ui/jellyfin_playback.cpp
    ${PELAGIA_FONTS_CPP}
)

target_include_directories(pelagia_core PUBLIC
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/platform
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(pelagia_core PUBLIC
    pelagia_log
    PkgConfig::JPS5_CURL
    pelagia_cjson
    PkgConfig::FFMPEG
    Threads::Threads
)

# UI -> platform bridge (Canvas, files, audio): the only part of the core that
# calls platform.h. Separate library so that the core tests do not have
# to link the platform layer.
add_library(pelagia_ui_platform STATIC
    ui/platform_bridge.cpp
)
target_link_libraries(pelagia_ui_platform PUBLIC pelagia_core pelagia_platform)
