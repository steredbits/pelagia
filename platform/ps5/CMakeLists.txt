# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# PS5 platform layer: shared SDL2 code (platform/sdl, software
# renderer on the console), POSIX files (platform/posix) and PS5
# specifics (folders under /data/homebrew/Pelagia, logs, full screen).
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_library(pelagia_platform STATIC
    ${CMAKE_SOURCE_DIR}/platform/sdl/platform_sdl.cpp
    ${CMAKE_SOURCE_DIR}/platform/posix/files_posix.cpp
    platform_ps5.cpp
)

target_include_directories(pelagia_platform
    PUBLIC ${CMAKE_SOURCE_DIR}/platform
    PRIVATE ${CMAKE_SOURCE_DIR}/platform/sdl)
target_link_libraries(pelagia_platform
    PUBLIC pelagia_log
    PRIVATE Threads::Threads PkgConfig::SDL2
)
