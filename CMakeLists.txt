# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
cmake_minimum_required(VERSION 3.16)
# Version number: single source (shown in the application, sent to the Jellyfin
# server, compared with the tag by the release: tools/check-version.sh).
project(pelagia VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Target platform: linux (development/tests) or ps5
# ---------------------------------------------------------------------------
set(PLATFORM "linux" CACHE STRING "Plateforme cible (linux|ps5)")
set_property(CACHE PLATFORM PROPERTY STRINGS linux ps5)

if(NOT PLATFORM STREQUAL "linux" AND NOT PLATFORM STREQUAL "ps5")
    message(FATAL_ERROR "PLATFORM must be 'linux' or 'ps5' (got: '${PLATFORM}')")
endif()

message(STATUS "Pelagia: target platform = ${PLATFORM}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Checking build: libstdc++ assertions (operator[] and iterators
# checked, as on Fedora) + AddressSanitizer + UBSan. Linux and GCC/Clang
# only; run by CI (the "sanitizers" job). ctest timeouts are
# multiplied: sanitizers slow execution down by 2 to 3 times.
option(PELAGIA_SANITIZE "libstdc++ assertions + ASan + UBSan (checking Debug build)" OFF)
set(PELAGIA_TIMEOUT_FACTOR 1)
if(PELAGIA_SANITIZE)
    if(NOT PLATFORM STREQUAL "linux")
        message(FATAL_ERROR "PELAGIA_SANITIZE is only meant for PLATFORM=linux")
    endif()
    message(STATUS "Pelagia: libstdc++ assertions + ASan + UBSan enabled")
    add_compile_options(-D_GLIBCXX_ASSERTIONS -fsanitize=address,undefined
                        -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
    set(PELAGIA_TIMEOUT_FACTOR 3)
endif()

# The core must stay portable to the PS5 toolchain: no exceptions, no RTTI.
add_compile_options(-Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

# cJSON is vendored in the repository (third_party/cjson): PacBrew does not provide it for the
# PS5; the same copy is used on Linux.
add_library(pelagia_cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(pelagia_cjson
    PUBLIC third_party/cjson/include PRIVATE third_party/cjson/include/cjson)
# Unmodified third-party code: warnings (isnan macro of the FreeBSD libc) are silenced.
target_compile_options(pelagia_cjson PRIVATE -w)

add_subdirectory(core)
add_subdirectory(platform/${PLATFORM})

add_executable(pelagia core/main.cpp)
target_link_libraries(pelagia PRIVATE pelagia_ui_platform pelagia_core pelagia_platform)

# PS5: only the application is built (ELF payload), not the development
# tools or the tests, which run on Linux.
if(PLATFORM STREQUAL "ps5")
    set_target_properties(pelagia PROPERTIES OUTPUT_NAME pelagia.elf)
    # websrv homebrew folder ready to copy to /data/homebrew/Pelagia:
    # eboot.elf (as linked, with symbols: ci/build-ps5.sh then strips it and keeps the
    # original as pelagia-debug-symbols.elf), icon, example
    # pre-configuration file (never copied under the active name).
    set(PELAGIA_PKG_DIR ${CMAKE_BINARY_DIR}/Pelagia)
    add_custom_command(TARGET pelagia POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PELAGIA_PKG_DIR}/sce_sys
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:pelagia> ${PELAGIA_PKG_DIR}/eboot.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/pkg/icon0.png
                ${PELAGIA_PKG_DIR}/sce_sys/icon0.png
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/pkg/pelagia.conf.example
                ${PELAGIA_PKG_DIR}/pelagia.conf.example
        COMMENT "Dossier homebrew : ${PELAGIA_PKG_DIR}"
        VERBATIM)
    return()
endif()

# CLI tool: authentication + movie list (manual API check).
add_executable(pelagia-cli core/cli_main.cpp)
target_link_libraries(pelagia-cli PRIVATE pelagia_core)

# Playback tool: local file (windowed or --headless).
add_executable(pelagia-play core/play_main.cpp)
target_link_libraries(pelagia-play PRIVATE pelagia_core pelagia_platform)

if(PLATFORM STREQUAL "linux")
    enable_testing()
    add_subdirectory(tests)
endif()

# PNG screenshots of the screens (no display or GPU), for review.
add_executable(pelagia-capture core/capture_main.cpp)
target_link_libraries(pelagia-capture PRIVATE pelagia_ui_platform pelagia_core pelagia_platform)
