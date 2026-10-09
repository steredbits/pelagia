# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Core unit tests, run through ctest.

# ctest timeouts: multiplied with PELAGIA_SANITIZE (see root CMakeLists.txt).
math(EXPR T60 "60 * ${PELAGIA_TIMEOUT_FACTOR}")
math(EXPR T120 "120 * ${PELAGIA_TIMEOUT_FACTOR}")

function(pelagia_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pelagia_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pelagia_add_test(test_sanity test_sanity.cpp)
pelagia_add_test(test_log test_log.cpp)
pelagia_add_test(test_log_file test_log_file.cpp)
pelagia_add_test(test_preconfig test_preconfig.cpp)
pelagia_add_test(test_app_info test_app_info.cpp)
pelagia_add_test(test_i18n test_i18n.cpp)
pelagia_add_test(test_settings_store test_settings_store.cpp)
pelagia_add_test(test_ps5_paths test_ps5_paths.cpp)
target_include_directories(test_ps5_paths PRIVATE ${CMAKE_SOURCE_DIR}/platform/ps5)
pelagia_add_test(test_jellyfin_urls test_jellyfin_urls.cpp)
pelagia_add_test(test_jellyfin_json test_jellyfin_json.cpp)
pelagia_add_test(test_jellyfin_client test_jellyfin_client.cpp)
pelagia_add_test(test_text test_text.cpp)

# Audio and subtitle languages.
pelagia_add_test(test_languages test_languages.cpp)
pelagia_add_test(test_track_selection test_track_selection.cpp)
pelagia_add_test(test_track_labels test_track_labels.cpp)
pelagia_add_test(test_subtitles test_subtitles.cpp)
pelagia_add_test(test_ui_tracks test_ui_tracks.cpp)

# Interface.
pelagia_add_test(test_soft_canvas test_soft_canvas.cpp)
pelagia_add_test(test_image_codec test_image_codec.cpp)
pelagia_add_test(test_text_renderer test_text_renderer.cpp)
pelagia_add_test(test_disk_cache test_disk_cache.cpp)
pelagia_add_test(test_poster_cache test_poster_cache.cpp)
pelagia_add_test(test_jellyfin_session test_jellyfin_session.cpp)
pelagia_add_test(test_ui_format test_ui_format.cpp)
pelagia_add_test(test_ui_focus test_ui_focus.cpp)
pelagia_add_test(test_ui_navigation test_ui_navigation.cpp)

# Playback pipeline.
pelagia_add_test(test_bounded_queue test_bounded_queue.cpp)
pelagia_add_test(test_clock test_clock.cpp)
pelagia_add_test(test_av_sync test_av_sync.cpp)
pelagia_add_test(test_seek_offset test_seek_offset.cpp)
pelagia_add_test(test_buffering test_buffering.cpp)
pelagia_add_test(test_retry_policy test_retry_policy.cpp)
pelagia_add_test(test_playback_monitor test_playback_monitor.cpp)

# Integration: MP4 files synthesized through libav (no ffmpeg binary
# required). The TIMEOUT catches any thread-shutdown deadlock.
add_executable(test_player test_player.cpp media_synth.cpp)
target_link_libraries(test_player PRIVATE pelagia_core)
target_include_directories(test_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_player COMMAND test_player)
set_tests_properties(test_player PROPERTIES TIMEOUT ${T120})
set_tests_properties(test_bounded_queue PROPERTIES TIMEOUT ${T60})

# Player in the interface (synthesized MP4 files, real time).
add_executable(test_ui_player test_ui_player.cpp media_synth.cpp)
target_link_libraries(test_ui_player PRIVATE pelagia_core)
target_include_directories(test_ui_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_ui_player COMMAND test_ui_player)
set_tests_properties(test_ui_player PROPERTIES TIMEOUT ${T120})

# Display geometry of the SDL platform (pure header, no SDL).
add_executable(test_letterbox test_letterbox.cpp)
target_include_directories(test_letterbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/platform/sdl)
add_test(NAME test_letterbox COMMAND test_letterbox)

# Text input policy (a single keyboard on PS5, pure header).
add_executable(test_text_input test_text_input.cpp)
target_include_directories(test_text_input PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/platform/sdl)
add_test(NAME test_text_input COMMAND test_text_input)

# Open gamepads of the SDL platform (pure header, no SDL).
add_executable(test_controller_set test_controller_set.cpp)
target_include_directories(test_controller_set PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/platform/sdl)
add_test(NAME test_controller_set COMMAND test_controller_set)

# Keyboard/gamepad mapping of the SDL platform (pure header, SDL constants).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_TESTS REQUIRED IMPORTED_TARGET sdl2)
add_executable(test_input_map test_input_map.cpp)
target_include_directories(test_input_map PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/platform/sdl ${CMAKE_SOURCE_DIR}/platform)
target_link_libraries(test_input_map PRIVATE PkgConfig::SDL2_TESTS)
add_test(NAME test_input_map COMMAND test_input_map)

# Platform files (POSIX, shared Linux/PS5).
add_executable(test_files_posix test_files_posix.cpp)
target_include_directories(test_files_posix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_files_posix PRIVATE pelagia_platform)
add_test(NAME test_files_posix COMMAND test_files_posix)

# pelagia-play CLI: exit codes, help, outputs (files generated by
# media_synth_tool in a dedicated working directory).
add_executable(media_synth_tool media_synth_tool.cpp media_synth.cpp)
target_link_libraries(media_synth_tool PRIVATE pelagia_core)
target_include_directories(media_synth_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_play_cli
    COMMAND ${CMAKE_COMMAND}
        -DPLAY=$<TARGET_FILE:pelagia-play>
        -DSYNTH=$<TARGET_FILE:media_synth_tool>
        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/play_cli_work
        -P ${CMAKE_CURRENT_SOURCE_DIR}/play_cli_test.cmake)
set_tests_properties(test_play_cli PROPERTIES TIMEOUT ${T120})

# Fake Jellyfin server (tools/fake_jellyfin_server.py): python3,
# ffmpeg and ffprobe required. Without them, the network tests are disabled and a
# message says so (code 77 = test skipped if a tool is missing at run time).
find_package(Python3 COMPONENTS Interpreter)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(Python3_Interpreter_FOUND AND FFMPEG_EXECUTABLE)
    set(PELAGIA_NETWORK_TESTS ON)
    set(FAKE_SERVER_SCRIPT ${CMAKE_SOURCE_DIR}/tools/fake_jellyfin_server.py)
    set(FAKE_SERVER_WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/fake_server_work)
    add_test(NAME test_fake_server
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_fake_server.py
            --server ${FAKE_SERVER_SCRIPT} --workdir ${FAKE_SERVER_WORKDIR})
    set_tests_properties(test_fake_server PROPERTIES TIMEOUT ${T120} SKIP_RETURN_CODE 77)

    # Complete application (dummy SDL drivers): clean exit and logged
    # shutdown steps, with the sanitizers in the matching build.
    add_test(NAME test_app_shutdown
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app_shutdown.py
            --app $<TARGET_FILE:pelagia> --server ${FAKE_SERVER_SCRIPT}
            --workdir ${CMAKE_CURRENT_BINARY_DIR}/app_shutdown_work)
    set_tests_properties(test_app_shutdown PROPERTIES TIMEOUT ${T120}
        SKIP_RETURN_CODE 77
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    # C++ tests against the fake server (started as a subprocess).
    function(pelagia_add_network_test name source)
        add_executable(${name} ${source} fake_server.cpp)
        target_link_libraries(${name} PRIVATE pelagia_core)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${name} PRIVATE
            PELAGIA_PYTHON="${Python3_EXECUTABLE}"
            PELAGIA_FAKE_SERVER_SCRIPT="${FAKE_SERVER_SCRIPT}"
            PELAGIA_FAKE_SERVER_WORKDIR="${FAKE_SERVER_WORKDIR}"
            PELAGIA_PLAY_BIN="$<TARGET_FILE:pelagia-play>"
            PELAGIA_CLI_BIN="$<TARGET_FILE:pelagia-cli>"
            PELAGIA_CAPTURE_BIN="$<TARGET_FILE:pelagia-capture>")
        add_dependencies(${name} pelagia-play pelagia-cli pelagia-capture)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES TIMEOUT ${T120} SKIP_RETURN_CODE 77)
    endfunction()
    pelagia_add_network_test(test_network_playback test_network_playback.cpp)
    pelagia_add_network_test(test_tracks_playback test_tracks_playback.cpp)
    pelagia_add_network_test(test_ui_backend test_ui_backend.cpp)
    pelagia_add_network_test(test_ui_capture test_ui_capture.cpp)
else()
    set(PELAGIA_NETWORK_TESTS OFF)
    message(STATUS "python3 or ffmpeg not found: network tests (fake server) disabled")
endif()

# PS5 deployment script (--zip) with a simulated curl: bash and python3
# are enough, no network.
find_program(BASH_EXECUTABLE bash)
if(BASH_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(NAME test_deploy_zip
        COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_deploy_zip.sh
            ${CMAKE_SOURCE_DIR}/tools/deploy-ps5.sh ${CMAKE_CURRENT_BINARY_DIR}/deploy_zip_work)
    set_tests_properties(test_deploy_zip PROPERTIES TIMEOUT ${T60})
endif()

# License headers (SPDX) present in all the project's sources.
if(EXISTS ${CMAKE_SOURCE_DIR}/.git AND Python3_Interpreter_FOUND)
    add_test(NAME test_spdx_headers
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/spdx_headers.py --check)
    set_tests_properties(test_spdx_headers PROPERTIES TIMEOUT ${T60})
endif()

# Release tools (version/tag, notes, USB zip): bash and python3, no network.
if(BASH_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(NAME test_release_tools
        COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_release_tools.sh
            ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/release_tools_work)
    set_tests_properties(test_release_tools PROPERTIES TIMEOUT ${T60})
endif()

# Stripping of the PS5 payload (ci/strip-ps5.sh) on a small Linux executable.
find_program(READELF_EXECUTABLE readelf)
find_program(STRIP_EXECUTABLE NAMES llvm-strip-18 llvm-strip strip)
if(BASH_EXECUTABLE AND READELF_EXECUTABLE AND STRIP_EXECUTABLE)
    add_test(NAME test_strip_ps5
        COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_strip_ps5.sh
            ${CMAKE_SOURCE_DIR}/ci/strip-ps5.sh ${CMAKE_CURRENT_BINARY_DIR}/strip_ps5_work)
    set_tests_properties(test_strip_ps5 PROPERTIES TIMEOUT ${T60})
endif()

# Extra tests that need tools which are not part of every checkout (optional file: nothing
# happens when it is absent). It may add arguments to LINKS_ARGS.
set(LINKS_ARGS "")
include(${CMAKE_CURRENT_SOURCE_DIR}/dev_tools_tests.cmake OPTIONAL)

# Relative links of Markdown files (README, docs): no broken link.
if(Python3_Interpreter_FOUND)
    add_test(NAME test_markdown_links
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/check_links.py ${CMAKE_SOURCE_DIR}
            ${LINKS_ARGS})
    set_tests_properties(test_markdown_links PROPERTIES TIMEOUT ${T60})
endif()
