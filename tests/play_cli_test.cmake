# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Test of the pelagia-play CLI: exit codes and outputs.
#   cmake -DPLAY=<pelagia-play> -DSYNTH=<media_synth_tool> -DWORKDIR=<dir> -P play_cli_test.cmake

set(failures 0)

# check(<name> CODE <expected code> [OUT <regex>] [ERR <regex>] ARGS <arguments...>)
# OUT/ERR: regex on standard output / error. NOT_OUT: forbidden regex.
function(check name)
  cmake_parse_arguments(C "" "CODE;OUT;ERR;NOT_OUT" "ARGS" ${ARGN})
  execute_process(
    COMMAND ${PLAY} ${C_ARGS}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 60)
  set(ok TRUE)
  if(NOT "${code}" STREQUAL "${C_CODE}")
    message(STATUS "FAIL [${name}] code ${code}, expected ${C_CODE}")
    set(ok FALSE)
  endif()
  if(C_OUT AND NOT out MATCHES "${C_OUT}")
    message(STATUS "FAIL [${name}] stdout without /${C_OUT}/:\n${out}")
    set(ok FALSE)
  endif()
  if(C_ERR AND NOT err MATCHES "${C_ERR}")
    message(STATUS "FAIL [${name}] stderr without /${C_ERR}/:\n${err}")
    set(ok FALSE)
  endif()
  if(C_NOT_OUT AND out MATCHES "${C_NOT_OUT}")
    message(STATUS "FAIL [${name}] stdout contains /${C_NOT_OUT}/:\n${out}")
    set(ok FALSE)
  endif()
  if(ok)
    message(STATUS "ok    [${name}]")
  else()
    math(EXPR n "${failures} + 1")
    set(failures ${n} PARENT_SCOPE)
  endif()
endfunction()

file(MAKE_DIRECTORY ${WORKDIR})
set(missing ${WORKDIR}/does_not_exist.mp4)
set(not_media ${CMAKE_CURRENT_LIST_FILE})

# --- Help ---------------------------------------------------------------------
check(help-long   CODE 0 OUT "Usage: pelagia-play" ARGS --help)
check(help-short  CODE 0 OUT "Usage: pelagia-play" ARGS -h)
check(help-lists-options CODE 0 OUT "--headless.*--verbose" ARGS --help)
check(help-lists-keys    CODE 0 OUT "Space.*Left arrow.*Page Down.*Ctrl\\+Q" ARGS --help)
check(help-lists-pad     CODE 0 OUT "Start.*L1 / R1.*L2 / R2.*circle" ARGS --help)
check(help-lists-codes   CODE 0 OUT "Exit codes" ARGS --help)
# Help takes precedence over the other argument errors.
check(help-wins-over-unknown CODE 0 OUT "Usage" ARGS --bogus --help)
check(help-after-file        CODE 0 OUT "Usage" ARGS ${missing} -h)

# --- Incorrect arguments : code 2 ----------------------------------------------
check(no-args        CODE 2 ERR "No file" ARGS )
check(unknown-option CODE 2 ERR "Unknown option: --bogus" ARGS --bogus ${missing})
check(two-files      CODE 2 ERR "Only one file" ARGS a.mp4 b.mp4)
check(only-options   CODE 2 ERR "No file" ARGS --headless --verbose)

# Network playback (--item): usage errors, without a server.
check(item-without-id     CODE 2 ERR "--item expects" ARGS --item)
check(item-and-file       CODE 2 ERR "mutually exclusive" ARGS --item abc ${missing})
check(bad-stream-auth     CODE 2 ERR "--stream-auth expects" ARGS --stream-auth cookie --item abc)
set(ENV{JELLYFIN_URL} "")
check(item-without-env    CODE 2 ERR "JELLYFIN_URL" ARGS --item abc)
check(resume-without-item CODE 2 ERR "--resume is only used" ARGS --resume ${missing})
check(help-stream-auth-default CODE 0 OUT "default: header" ARGS --help)
check(help-lists-item     CODE 0 OUT "--item <id>.*JELLYFIN_URL.*--resume.*--stream-auth" ARGS --help)

# --- Opening errors : code 1 -------------------------------------------------
check(missing-file          CODE 1 ERR "cannot open" ARGS ${missing})
check(missing-file-headless CODE 1 ERR "cannot open" ARGS --headless ${missing})
check(not-a-media-file      CODE 1 ERR "cannot open" ARGS ${not_media})
check(directory             CODE 1 ARGS ${WORKDIR})
# "--": a name starting with '-' is a file, not an option.
check(double-dash-is-file   CODE 1 ERR "cannot open" ARGS -- --bogus)

# --- Real playback: "n/a" drift without audio, measured with audio ------------
foreach(kind av video audio)
  execute_process(COMMAND ${SYNTH} ${WORKDIR}/cli_${kind}.mp4 2 ${kind} RESULT_VARIABLE r)
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "cannot generate cli_${kind}.mp4 (${r})")
  endif()
endforeach()

check(headless-av-measured CODE 0 OUT "Max A/V drift        : [0-9]+ ms - SIMULATED"
      ARGS --headless ${WORKDIR}/cli_av.mp4)
check(headless-video-only-na CODE 0 OUT "Max A/V drift        : n/a" NOT_OUT "[0-9]+ ms - SIMULATED"
      ARGS --headless ${WORKDIR}/cli_video.mp4)
check(headless-audio-only-na CODE 0 OUT "Max A/V drift        : n/a"
      ARGS --headless ${WORKDIR}/cli_audio.mp4)

# Windowed playback (dummy SDL drivers) without an audio track: "n/a" too.
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_AUDIODRIVER} dummy)
check(windowed-video-only-na CODE 0 ERR "max A/V drift: n/a"
      ARGS ${WORKDIR}/cli_video.mp4)

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} failing case(s)")
endif()
