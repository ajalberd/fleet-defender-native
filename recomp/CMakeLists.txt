cmake_minimum_required(VERSION 3.20)
project(f14_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# 1. SDL3 (Windowing, OpenGL, Multi-HOTAS Joysticks, Audio)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.30
    GIT_SHALLOW TRUE
)

# 2. Dear ImGui (HOTAS Mapper GUI, Audio Mixer, Live RE Debugger)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0
    GIT_SHALLOW TRUE
)

# 3. libx86emu (Lightweight 16/32-bit Real-Mode x86 CPU core with full hooks)
FetchContent_Declare(
    libx86emu
    GIT_REPOSITORY https://github.com/wfeldt/libx86emu.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

# 4. Munt libmt32emu (Roland CM-32L / MT-32 emulation)
set(libmt32emu_SHARED OFF CACHE BOOL "" FORCE)
set(libmt32emu_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(libmt32emu_CPP_INTERFACE ON CACHE BOOL "" FORCE)
set(libmt32emu_WITH_INTERNAL_RESAMPLER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    munt
    GIT_REPOSITORY https://github.com/munt/munt.git
    GIT_TAG libmt32emu_2_8_6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR mt32emu
)

# 5. nlohmann/json (HOTAS & Title Profiles)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3 imgui libx86emu munt nlohmann_json)

# Build Dear ImGui static library (SDL3 + OpenGL3 backends)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3)

# Build libx86emu static library (using patched prim_ops.c with fixed OUTSB DS:SI addressing)
file(GLOB LIBX86EMU_SRCS "${libx86emu_SOURCE_DIR}/*.c")
list(FILTER LIBX86EMU_SRCS EXCLUDE REGEX ".*test.*")
list(FILTER LIBX86EMU_SRCS EXCLUDE REGEX ".*prim_ops\\.c$")
list(APPEND LIBX86EMU_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/compat/x86emu_prim_ops.c")
add_library(x86emu STATIC ${LIBX86EMU_SRCS})
target_include_directories(x86emu PUBLIC
    ${libx86emu_SOURCE_DIR}/include
    ${libx86emu_SOURCE_DIR}
)
if(WIN32)
    target_include_directories(x86emu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
endif()
if(MINGW OR UNIX)
    target_compile_options(x86emu PRIVATE -Wno-all -Wno-extra)
endif()

# Main executable (WIN32 GUI subsystem = PE Subsystem 2, zero console flash)
set(F14_EXTRA_SRCS "")
if(WIN32 AND MINGW)
    # Invoke windres from src/ with relative input so spaces in workspace path never break gcc -E preprocessing
    set(F14_RES_OBJ "${CMAKE_CURRENT_BINARY_DIR}/f14_native_res.o")
    add_custom_command(
        OUTPUT "${F14_RES_OBJ}"
        COMMAND "${CMAKE_RC_COMPILER}" --use-temp-file -O coff -i f14_native.rc -o "${F14_RES_OBJ}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/f14_native.rc" "${CMAKE_CURRENT_SOURCE_DIR}/src/f14_native.manifest"
        COMMENT "Compiling Windows GUI manifest resource (f14_native.rc)"
    )
    list(APPEND F14_EXTRA_SRCS "${F14_RES_OBJ}")
elseif(WIN32)
    list(APPEND F14_EXTRA_SRCS src/f14_native.rc)
endif()

add_executable(f14_native WIN32
    src/main.cpp
    src/dos/vga.cpp
    src/dos/dos_machine.cpp
    src/dos/f14_dos_config.cpp
    src/audio/audio_engine.cpp
    src/input/hotas_mapper.cpp
    src/net/net_udp.cpp
    src/render/cockpit_3d.cpp
    src/render/modern_renderer.cpp
    src/sim/f14_flight_model.cpp
    src/ui/overlay_ui.cpp
    src/ui/quick_keys.cpp
    src/ui/win_helpers.cpp
    ${F14_EXTRA_SRCS}
)

target_include_directories(f14_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(f14_native PRIVATE
    SDL3::SDL3
    imgui_lib
    x86emu
    mt32emu
    nlohmann_json::nlohmann_json
    opengl32
)

if(WIN32)
    target_link_libraries(f14_native PRIVATE winmm ws2_32 advapi32 shell32 ole32 uuid)
    if(MINGW)
        target_link_options(f14_native PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
    endif()
endif()
