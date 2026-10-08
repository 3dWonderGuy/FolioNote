cmake_minimum_required(VERSION 3.22)
project(FolioNote LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32 AND NOT ANDROID)
    set(CMAKE_MAKE_PROGRAM "/usr/bin/ninja" CACHE FILEPATH "Path to ninja build program" FORCE)
endif()

# --- Output directory configuration ---
if(NOT ANDROID)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
endif()

# --- MSVC PDB / Path Length Fix ---
if(MSVC)
    set(CMAKE_OBJECT_PATH_MAX 1024)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    # /FS    — force synchronous PDB writes (needed for parallel builds)
    # /bigobj — raise COFF section limit from 65,535 to ~16M sections.
    # /utf-8  — set both source and execution character sets to UTF-8 so that
    #           std::string and std::filesystem::path handle Unicode/non-ASCII characters seamlessly.
    add_compile_options(/FS /bigobj /utf-8)
    add_compile_definitions(NOMINMAX)
endif()

# System OpenGL
if(NOT ANDROID)
    if(EXISTS "/usr/lib64/libGL.so.1")
        set(OPENGL_gl_LIBRARY "/usr/lib64/libGL.so.1")
    elseif(EXISTS "/usr/lib64/libGL.so")
        set(OPENGL_gl_LIBRARY "/usr/lib64/libGL.so")
    elseif(EXISTS "/usr/lib/libGL.so.1")
        set(OPENGL_gl_LIBRARY "/usr/lib/libGL.so.1")
    elseif(EXISTS "/usr/lib/libGL.so")
        set(OPENGL_gl_LIBRARY "/usr/lib/libGL.so")
    else()
        find_package(OpenGL QUIET)
    endif()

    if(OPENGL_gl_LIBRARY AND NOT TARGET OpenGL::GL)
        message(STATUS "Using OpenGL library: ${OPENGL_gl_LIBRARY}")
        add_library(OpenGL::GL UNKNOWN IMPORTED)
        set_target_properties(OpenGL::GL PROPERTIES
            IMPORTED_LOCATION "${OPENGL_gl_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include"
        )
        set(OpenGL_FOUND TRUE CACHE BOOL "" FORCE)
        set(OPENGL_FOUND TRUE CACHE BOOL "" FORCE)
    endif()
endif()

# Disable tests and sample builds
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
set(BLEND2D_TEST OFF CACHE BOOL "" FORCE)
set(BLEND2D_DEMOS OFF CACHE BOOL "" FORCE)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Direct ink-stroke-modeler FetchContent to local abseil directory
set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_ABSEIL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp" CACHE PATH "" FORCE)

# Force static linking for submodules
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

# 1. Add Local Dependencies with Short Binary Dirs to prevent Windows MAX_PATH overflows
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/ink-stroke-modeler ink_sm EXCLUDE_FROM_ALL)
add_subdirectory(third_party/SDL sdl EXCLUDE_FROM_ALL)
add_subdirectory(third_party/blend2d b2d EXCLUDE_FROM_ALL)
add_subdirectory(third_party/lunasvg lsvg EXCLUDE_FROM_ALL)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_image/CMakeLists.txt")
    set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP_VENDORED ON CACHE BOOL "" FORCE)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/SDL_image sdl_img EXCLUDE_FROM_ALL)
endif()

# 2. SQLite3 Static Library
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
    add_library(sqlite3 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
    target_include_directories(sqlite3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3")
    target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5=1 SQLITE_ENABLE_JSON1=1)
endif()

# 3. ImGui Configuration
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)

# 4. App Source Files
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
list(FILTER APP_SOURCES EXCLUDE REGEX "android-project")

# 5. Application Target
if(ANDROID)
    # Android MUST compile to libmain.so for SDLActivity to load it
    add_library(main SHARED
        ${APP_SOURCES}
        ${IMGUI_SOURCES}
    )
    # Ensure exported symbols (like SDL_main) are visible to dlsym
    set_target_properties(main PROPERTIES
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN 0
    )
    set(APP_TARGET main)
else()
    # Desktop build produces FolioNote executable
    if(WIN32)
        set(WIN32_RESOURCES src/folionote.rc)
    endif()
    add_executable(FolioNote
        ${APP_SOURCES}
        ${IMGUI_SOURCES}
        ${WIN32_RESOURCES}

    )
    set(APP_TARGET FolioNote)
endif()

# 6. Target Include Directories
target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blend2d
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blend2d/blend2d
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_image/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Compile Definitions
target_compile_definitions(${APP_TARGET} PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
)

# 6.1 Precompiled Headers (PCH) for Fast Compilation
target_precompile_headers(${APP_TARGET} PRIVATE
    <vector>
    <string>
    <memory>
    <algorithm>
    <functional>
    <unordered_map>
    <unordered_set>
    <cmath>
    <cstdint>
    <utility>
    <filesystem>
    <chrono>
    <sstream>
    <fstream>
    <blend2d/blend2d.h>
    <SDL3/SDL.h>
    <imgui.h>
)

# 7. Link Dependencies
target_link_libraries(${APP_TARGET} PRIVATE
    blend2d::blend2d
    lunasvg::lunasvg
    SDL3_image::SDL3_image
    InkStrokeModeler::stroke_modeler
    sqlite3
)

if(ANDROID)
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_GLES3_LIB GLESv3)
    find_library(ANDROID_EGL_LIB EGL)

    target_compile_definitions(main PRIVATE IMGUI_IMPL_OPENGL_ES3)

    target_link_libraries(main PRIVATE
        SDL3::SDL3-static
        ${ANDROID_LOG_LIB}
        ${ANDROID_LIB}
        ${ANDROID_GLES3_LIB}
        ${ANDROID_EGL_LIB}
    )
else()
    target_link_libraries(FolioNote PRIVATE
        SDL3::SDL3
        OpenGL::GL
    )
    if(WIN32)
        target_link_libraries(FolioNote PRIVATE dwmapi ole32 oleaut32 shell32 winspool windowscodecs)
    endif()
endif()

# 8. Post-Build Asset Deployment
if(NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(
        TARGET FolioNote POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
        COMMENT "Deploying assets to build output directory..."
    )
endif()

# 9. PDFium Cross-Platform Library Integration
set(PDFIUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
if(EXISTS "${PDFIUM_DIR}")
    set(FOLIO_PDFIUM_FOUND OFF)
    if(WIN32 AND EXISTS "${PDFIUM_DIR}/lib/pdfium.dll.lib")
        set(FOLIO_PDFIUM_FOUND ON)
        target_include_directories(${APP_TARGET} PRIVATE "${PDFIUM_DIR}/include")
        target_compile_definitions(${APP_TARGET} PRIVATE FOLIO_HAS_PDFIUM=1)
        target_link_libraries(${APP_TARGET} PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib")
        if(EXISTS "${PDFIUM_DIR}/bin/pdfium.dll")
            add_custom_command(
                TARGET FolioNote POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PDFIUM_DIR}/bin/pdfium.dll"
                "$<TARGET_FILE_DIR:${APP_TARGET}>/pdfium.dll"
                COMMENT "Deploying pdfium.dll to output directory..."
            )
        endif()
    elseif(ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/android-project/app/src/main/jniLibs/${ANDROID_ABI}/libpdfium.so")
        set(FOLIO_PDFIUM_FOUND ON)
        target_include_directories(${APP_TARGET} PRIVATE "${PDFIUM_DIR}/include")
        target_compile_definitions(${APP_TARGET} PRIVATE FOLIO_HAS_PDFIUM=1)
        target_link_libraries(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/android-project/app/src/main/jniLibs/${ANDROID_ABI}/libpdfium.so")
    elseif(UNIX AND NOT APPLE AND NOT ANDROID AND EXISTS "${PDFIUM_DIR}/lib/libpdfium.so")
        set(FOLIO_PDFIUM_FOUND ON)
        target_include_directories(${APP_TARGET} PRIVATE "${PDFIUM_DIR}/include")
        target_compile_definitions(${APP_TARGET} PRIVATE FOLIO_HAS_PDFIUM=1)
        target_link_libraries(${APP_TARGET} PRIVATE "${PDFIUM_DIR}/lib/libpdfium.so")
        add_custom_command(
            TARGET FolioNote POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFIUM_DIR}/lib/libpdfium.so"
            "$<TARGET_FILE_DIR:${APP_TARGET}>/libpdfium.so"
            COMMENT "Deploying libpdfium.so to output directory..."
        )
    endif()

    if(FOLIO_PDFIUM_FOUND)
        message(STATUS "PDFium library found and enabled for ${CMAKE_SYSTEM_NAME}")
    else()
        message(STATUS "PDFium library not found for ${CMAKE_SYSTEM_NAME}; building with PDF viewer stubs")
    endif()
endif()





# =============================================================================
# 10. libVLC Media Player Integration (Video Container Subsystem)
# =============================================================================
# Strategy: Look for the VLC SDK in three locations, in priority order:
#   A. Vendored SDK: third_party/libvlc/sdk/ (preferred — version-locked, reproducible builds)
#   B. System VLC on Windows: C:/Program Files/VideoLAN/VLC/ (user has VLC installed)
#   C. Not found: Build without libVLC (FOLIO_HAS_LIBVLC not defined); video objects
#      fall back to placeholder card rendering with no actual decode capability.
#      A clear CMake STATUS message is emitted so developers know what's missing.
#
# libVLC linking model (Windows DLL):
#   - Link against the .lib import library at build time.
#   - Deploy libvlc.dll + libvlccore.dll + plugins/ to the output directory post-build.
#   - At runtime, VLC locates its plugin folder by searching for libvlccore.dll relative
#     to the executable via LIBVLC_PLUGIN_PATH or the plugins/ directory next to the DLL.
# =============================================================================

if(NOT ANDROID)
    set(FOLIO_VLC_FOUND OFF)

    # ── A. Check vendored Windows SDK (third_party/libvlc/sdk/) ──────────────
    set(FOLIO_VLC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvlc/sdk")
    if(WIN32 AND EXISTS "${FOLIO_VLC_SDK_DIR}/include/vlc/libvlc.h" AND
       EXISTS "${FOLIO_VLC_SDK_DIR}/lib/libvlc.lib")

        set(FOLIO_VLC_FOUND ON)
        set(FOLIO_VLC_INCLUDE_DIR "${FOLIO_VLC_SDK_DIR}/include")
        set(FOLIO_VLC_LIB_FILE    "${FOLIO_VLC_SDK_DIR}/lib/libvlc.lib")
        set(FOLIO_VLC_BIN_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvlc/bin")
        message(STATUS "libVLC: Found vendored Windows SDK at ${FOLIO_VLC_SDK_DIR}")

    # ── B. Check system VLC installation on Windows ──────────────────────────
    elseif(WIN32 AND EXISTS "C:/Program Files/VideoLAN/VLC/sdk/lib/libvlc.lib")

        set(FOLIO_VLC_FOUND ON)
        set(FOLIO_VLC_INCLUDE_DIR "C:/Program Files/VideoLAN/VLC/sdk/include")
        set(FOLIO_VLC_LIB_FILE    "C:/Program Files/VideoLAN/VLC/sdk/lib/libvlc.lib")
        set(FOLIO_VLC_BIN_DIR     "C:/Program Files/VideoLAN/VLC")
        message(STATUS "libVLC: Found system installation at C:/Program Files/VideoLAN/VLC")

    # ── C. Check system VLC on Linux / Unix ──────────────────────────────────
    elseif(UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(LIBVLC QUIET libvlc)
            if(LIBVLC_FOUND)
                set(FOLIO_VLC_FOUND ON)
                set(FOLIO_VLC_INCLUDE_DIR ${LIBVLC_INCLUDE_DIRS})
                set(FOLIO_VLC_LIB_FILE    ${LIBVLC_LIBRARIES})
                message(STATUS "libVLC: Found system libvlc via pkg-config")
            endif()
        endif()

    else()
        message(STATUS "libVLC: SDK not found. VideoObject will use placeholder card only.")
        message(STATUS "libVLC: To enable on Windows: place sdk/include/vlc/ and sdk/lib/libvlc.lib in third_party/libvlc/sdk/")
    endif()

    if(NOT FOLIO_VLC_FOUND)
        message(STATUS "libVLC: SDK not found. VideoObject will use placeholder card only.")
    endif()

    if(FOLIO_VLC_FOUND)
        # Wire include path, compile definition, and import library
        target_include_directories(${APP_TARGET} PRIVATE "${FOLIO_VLC_INCLUDE_DIR}")
        target_compile_definitions(${APP_TARGET} PRIVATE FOLIO_HAS_LIBVLC=1)
        target_link_libraries(${APP_TARGET} PRIVATE "${FOLIO_VLC_LIB_FILE}")

        if(WIN32)
            # Deploy libvlc.dll and libvlccore.dll alongside the executable
            # If they are in the vendored bin/ directory, copy from there.
            # Otherwise try the system VLC directory.
            foreach(_vlcdll libvlc.dll libvlccore.dll)
                set(_vlcdll_src "${FOLIO_VLC_BIN_DIR}/${_vlcdll}")
                if(EXISTS "${_vlcdll_src}")
                    add_custom_command(
                        TARGET ${APP_TARGET} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_vlcdll_src}"
                        "$<TARGET_FILE_DIR:${APP_TARGET}>/${_vlcdll}"
                        COMMENT "Deploying ${_vlcdll} to output directory..."
                    )
                else()
                    # Fallback: try system VLC even if SDK was vendored
                    set(_vlcdll_sys "C:/Program Files/VideoLAN/VLC/${_vlcdll}")
                    if(EXISTS "${_vlcdll_sys}")
                        add_custom_command(
                            TARGET ${APP_TARGET} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_vlcdll_sys}"
                            "$<TARGET_FILE_DIR:${APP_TARGET}>/${_vlcdll}"
                            COMMENT "Deploying system ${_vlcdll} to output directory..."
                        )
                    else()
                        message(WARNING "libVLC: Cannot find ${_vlcdll} to deploy — video playback may fail at runtime.")
                    endif()
                endif()
            endforeach()

            # Deploy the VLC plugins directory (essential for hardware decoding, network streams, codecs)
            # Without plugins/, libvlc_new() fails to load any codecs.
            foreach(_vlcplugindir "${FOLIO_VLC_BIN_DIR}/plugins" "C:/Program Files/VideoLAN/VLC/plugins")
                if(EXISTS "${_vlcplugindir}")
                    add_custom_command(
                        TARGET ${APP_TARGET} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_vlcplugindir}"
                        "$<TARGET_FILE_DIR:${APP_TARGET}>/plugins"
                        COMMENT "Deploying VLC plugins directory to output..."
                    )
                    break()
                endif()
            endforeach()

            # Deploy the VLC lua directory (essential for playlist scrapers, meta readers, art extractors)
            foreach(_vlcluadir "${FOLIO_VLC_BIN_DIR}/lua" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvlc/extract/vlc-3.0.20/lua" "C:/Program Files/VideoLAN/VLC/lua")
                if(EXISTS "${_vlcluadir}")
                    add_custom_command(
                        TARGET ${APP_TARGET} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_vlcluadir}"
                        "$<TARGET_FILE_DIR:${APP_TARGET}>/lua"
                        COMMENT "Deploying VLC lua directory to output..."
                    )
                    break()
                endif()
            endforeach()

            if(MSVC OR WIN32)
                target_link_libraries(${APP_TARGET} PRIVATE urlmon)
            endif()
        endif()

        message(STATUS "libVLC: Integration enabled — VideoObject will support full media playback")
    endif()
endif()

# 11. Cross-Platform Embedded WebView Subsystem (for Web Embeds & Native YouTube)
if(WIN32)
    set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/webview)
    target_link_libraries(${APP_TARGET} PRIVATE webview::core)
endif()



set(FOLIO_IO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/path_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/app_directories.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/system_dialogs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/file_writer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/file_reader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/file_manager.cpp"
)

# testing
# 10. Standalone Local Test Executable
if(NOT ANDROID)
    add_executable(test_rtree
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_rtree.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/spatial/r_tree.cpp"
        ${FOLIO_IO_SOURCES}
    )

    target_include_directories(test_rtree PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/blend2d"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/blend2d/blend2d"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include"
    )

    target_link_libraries(test_rtree PRIVATE
        blend2d::blend2d
        SDL3::SDL3
    )

    if(WIN32)
        target_link_libraries(test_rtree PRIVATE dwmapi ole32 shell32 winspool)
    endif()

    set_target_properties(test_rtree PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# 11. Standalone Local Test Executable: FileManager
if(NOT ANDROID)
    add_executable(test_file_manager
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_file_manager.cpp"
        ${FOLIO_IO_SOURCES}
    )

    target_include_directories(test_file_manager PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include"
    )

    target_link_libraries(test_file_manager PRIVATE
        SDL3::SDL3
    )

    if(WIN32)
        target_link_libraries(test_file_manager PRIVATE dwmapi ole32 shell32 winspool)
    endif()

    set_target_properties(test_file_manager PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# 12. Standalone Local Test Executable: ThreadPool
if(NOT ANDROID)
    add_executable(test_thread_pool
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_thread_pool.cpp"
        ${FOLIO_IO_SOURCES}
    )

    target_include_directories(test_thread_pool PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include"
    )

    target_link_libraries(test_thread_pool PRIVATE
        SDL3::SDL3
    )

    if(WIN32)
        target_link_libraries(test_thread_pool PRIVATE dwmapi ole32 shell32 winspool)
    endif()

    set_target_properties(test_thread_pool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# 13. Standalone Local Test Executable: FileSaver
if(NOT ANDROID)
    add_executable(test_file_saver
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_file_saver.cpp"
        ${FOLIO_IO_SOURCES}
    )

    target_include_directories(test_file_saver PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include"
    )

    target_link_libraries(test_file_saver PRIVATE
        SDL3::SDL3
    )

    if(WIN32)
        target_link_libraries(test_file_saver PRIVATE dwmapi ole32 shell32 winspool)
    endif()

    set_target_properties(test_file_saver PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()
