cmake_minimum_required(VERSION 3.14)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_ERROR_DEPRECATED OFF CACHE BOOL "" FORCE)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Define VITASDK env var or set CMAKE_TOOLCHAIN_FILE.")
endif ()

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(VITA_REAL_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Resolved VitaSDK toolchain file")
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/vita.toolchain.wrapper.cmake" CACHE PATH "Toolchain file")
endif ()

# The Vita toolchain's crt0.o is softfp while gcc defaults to hard-float VFP,
# so CMake's default "link a test exe" compiler check fails on an ABI mismatch.
# Make the check compile-only (static lib) to skip the link step entirely.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(pvz2_loader C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Plants vs Zombies 2")
set(VITA_TITLEID  "PVZ2VITA1")
set(VITA_VPKNAME  "pvz2-vita-452-60fps")
set(VITA_VERSION  "01.00")
# ATTRIBUTE2=12 grants the extended memory budget the game needs.
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP)")

# Game data lives under ux0:data/pvz2/ (the .so's + the .obb RSB archive).
set(GAME_DATA_PATH "ux0:data/pvz2/" CACHE STRING "Path to supplied game files (trailing /)")
set(DATA_PATH "${GAME_DATA_PATH}userdata/")
add_definitions(-DDATA_PATH="${DATA_PATH}")
add_definitions(-DGAME_DATA_PATH="${GAME_DATA_PATH}")

# vitaGL GLES path (no PVR native driver, no SDL, no FMOD).
option(USE_SCELIBC_IO "Use SceLibcBridge for IO" OFF)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

# Verbose loader/FalsoJNI telemetry to ux0:data/pvz2/loader.log for bring-up.
add_definitions(-DDEBUG_SOLOADER -DFALSOJNI_DEBUGLEVEL=3)

# RENDER BACKEND (2026-07-06): switched PVR -> vitaGL. The native PVR driver
# rendered to complete FBOs with real textures but showed BLACK on screen (the
# tile-based-deferred resolve / texture-display issue Max hit on the MCSM port,
# which vitaGL fixed). vitaGL's known hurdle is PvZ2's shaders vs its ShaccCg
# runtime compiler — tackled separately. To revert: re-add -DUSE_PVR_PSP2 and the
# PVR stubs/sources below.
# add_definitions(-DUSE_PVR_PSP2)
add_definitions(-DUSE_GLSL_SHADERS)

add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated -fno-unwind-tables -fno-asynchronous-unwind-tables")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables")

# CMake's trailing Debug -O0 used to override the earlier -O1. Keep symbols
# while optimizing the real Vita hot paths; preserve strict floating point.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g3")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g0")

# Keep ARM ABI mismatch diagnostics enabled. All libraries come from the
# softfp SDK selected by scripts/build-vita.ps1.

add_executable(${CMAKE_PROJECT_NAME}
               source/main_452.c
               source/dynlib.c
               source/dynlib_pvz2_gap.c
               source/reimpl/math_softfp.c
               source/java.c
               source/reimpl/bits/_ctype.c
               source/reimpl/controls.c
               source/reimpl/egl.c
               source/reimpl/errno.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/miniz_zlib.c
               source/reimpl/numeric_input.c
               source/reimpl/opensl_audio.c
               source/reimpl/pthr.c
               source/reimpl/rsb_index_vita.cpp
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/dialog.c
               source/utils/boot_check.c
               source/utils/data_layout.c
               source/utils/boot_screen.c
               source/utils/glutil.c
               source/utils/launch_state.c
               source/utils/loading_screen.c
               source/utils/placement_452.c
               source/utils/pixel_workers.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/telemetry.c
               source/utils/utils.c
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/falso_jni/converter.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c)

# math_softfp.c uses the same softfp ABI as the game AND this SDK's newlib.
# Forcing this file to hard-float corrupts sincos pointer arguments and the
# return values from strtod/strtof, even though every import resolves.

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib/kubridge)
target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib/pvr/include)

# PVR import stubs (generated from the PVR_PSP2 NID DB). Archive names are
# double-lib-prefixed (liblibGLESv2_stub.a) -> link as -llibGLESv2_stub.
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
                        ${CMAKE_SOURCE_DIR}/lib/pvr/stubs_gen)

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCg_stub
                      SceShaccCgExt
                      mathneon
                      OpenSLES
                      m
                      taihen_stub
                      ${CMAKE_SOURCE_DIR}/lib/kubridge/libkubridge_stub.a
                      SceSysmem_stub
                      SceKernelThreadMgr_stub
                      SceKernelModulemgr_stub
                      SceKernelDmacMgr_stub
                      SceSysmodule_stub
                      SceUlt_stub
                      SceIofilemgr_stub
                      SceAppUtil_stub
                      SceAppMgr_stub
                      SceCtrl_stub
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceDisplay_stub
                      SceGxm_stub
                      SceNet_stub
                      SceNetCtl_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      ScePower_stub
                      SceRtc_stub
                      SceSblSsMgr_stub
                      SceVshBridge_stub
                      SceCommonDialog_stub
                      SceIme_stub
                      SceFios2_stub
                      taihen_stub)

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE)

if (EXISTS "${CMAKE_SOURCE_DIR}/extras/livearea/files.cmake")
  include("${CMAKE_SOURCE_DIR}/extras/livearea/files.cmake")
  vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                  VERSION ${VITA_VERSION}
                  NAME ${VITA_APP_NAME}
                  FILE ${PVZ2_LIVEAREA_FILES}
                  # Bionic /dev/urandom is translated to app0:/urandom by the
                  # direct loader.  PvZ2 4.5.2 constructor 944 requires it.
                  FILE ${CMAKE_SOURCE_DIR}/extras/urandom urandom
                  FILE ${CMAKE_SOURCE_DIR}/extras/rsb452.idx rsb452.idx)
else()
  message(FATAL_ERROR "LiveArea package is missing. Run scripts/prepare-livearea.py before configuring the Vita target.")
endif()
