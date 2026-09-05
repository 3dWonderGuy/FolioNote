cmake_minimum_required(VERSION 3.22)
project(FolioNote LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Output directory configuration ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

# --- MSVC PDB / Path Length Fix ---
if(MSVC)
    set(CMAKE_OBJECT_PATH_MAX 1024)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    add_compile_options(/FS)
endif()

# System OpenGL
if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

# Disable tests and sample builds
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BLEND2D_TEST OFF CACHE BOOL "" FORCE)
set(BLEND2D_DEMOS OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

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
)

# 4. App Source Files
file(GLOB_RECURSE APP_SOURCES
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
    add_executable(FolioNote
        ${APP_SOURCES}
        ${IMGUI_SOURCES}
    )
    set(APP_TARGET FolioNote)
endif()

# 6. Target Include Directories
target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blend2d/blend2d
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# 7. Link Dependencies
target_link_libraries(${APP_TARGET} PRIVATE
    blend2d::blend2d
    lunasvg::lunasvg
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