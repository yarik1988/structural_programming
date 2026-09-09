cmake_minimum_required(VERSION 3.16)
project(GRAPHICS)
set(CMAKE_CXX_STANDARD 17)
find_package(OpenGL REQUIRED)
include(FetchContent)

message(STATUS "C++ compiler id: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_LINKER: ${CMAKE_LINKER}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(SFML_STATIC_LIBRARIES ON CACHE BOOL "Build static libraries" FORCE)
set(SFML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.2
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)

FetchContent_Declare(ImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG master
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

# PATCH_COMMAND removes a leftover debug message(WARNING ...) in ImGui-SFML's
# FindImGui.cmake that fires on every configure even on success
# (see cmake/patch_imgui_sfml.cmake).
FetchContent_Declare(ImGui-SFML
        GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
        GIT_TAG master
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM
        PATCH_COMMAND ${CMAKE_COMMAND}
                -DPATCH_FILE=<SOURCE_DIR>/cmake/FindImGui.cmake
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_imgui_sfml.cmake)

FetchContent_MakeAvailable(SFML)
set(SFML_DIR "${SFML_BINARY_DIR}" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "${SFML_BINARY_DIR}")


FetchContent_MakeAvailable(ImGui)
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "Disable find_package in ImGui-SFML, as SFML is provided by the parent project" FORCE)
FetchContent_MakeAvailable(ImGui-SFML)
target_compile_definitions(ImGui-SFML PRIVATE NDEBUG)

add_definitions(-DSFML_STATIC)

# --- Executables ---
add_executable(ROTATING_SQUARE square.cpp)
target_link_libraries(ROTATING_SQUARE PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(SPRITES Sprites.cpp)
target_link_libraries(SPRITES PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(SCREENER Screener.cpp glad/glad.c)
target_link_libraries(SCREENER PRIVATE SFML::Graphics SFML::Window SFML::System ImGui-SFML::ImGui-SFML)

# 1 = windowed, 2 = fullscreen. Override at configure time with
# -DSCREENER_WINDOW_MODE=<1|2>.
set(SCREENER_WINDOW_MODE 1 CACHE STRING "Screener window mode: 1=windowed, 2=fullscreen")
set_property(CACHE SCREENER_WINDOW_MODE PROPERTY STRINGS 1 2)
if (NOT SCREENER_WINDOW_MODE STREQUAL "1" AND NOT SCREENER_WINDOW_MODE STREQUAL "2")
    message(FATAL_ERROR "SCREENER_WINDOW_MODE must be 1 (windowed) or 2 (fullscreen)")
endif()
target_compile_definitions(SCREENER PRIVATE SCREENER_WINDOW_MODE=${SCREENER_WINDOW_MODE})

add_executable(FRAC_TREE FracTree.cpp)
target_link_libraries(FRAC_TREE PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(BUSY_TURTLE BusyTurtle.cpp)
target_link_libraries(BUSY_TURTLE PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(GAME_LIFE GameLife.cpp)
target_link_libraries(GAME_LIFE PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(3D 3D.cpp)
target_link_libraries(3D PRIVATE SFML::Window SFML::System)

add_executable(GRAPH Graph.cpp)
target_link_libraries(GRAPH PRIVATE SFML::Graphics SFML::Window SFML::System ImGui-SFML::ImGui-SFML)

add_executable(CYCLOID Cycloid.cpp)
target_link_libraries(CYCLOID PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(FLOODFILL FloodFill.cpp)
target_link_libraries(FLOODFILL PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(INTERVAL_INTERSECTION IntervalIntersection.cpp)
target_link_libraries(INTERVAL_INTERSECTION PRIVATE SFML::Graphics SFML::Window SFML::System)

add_executable(TRIANGLE_SQUARE TriangleSquare.cpp)
target_link_libraries(TRIANGLE_SQUARE PRIVATE SFML::Graphics SFML::Window SFML::System)

add_custom_target(sem1_graphics DEPENDS ROTATING_SQUARE SPRITES SCREENER FRAC_TREE BUSY_TURTLE GAME_LIFE 3D GRAPH CYCLOID FLOODFILL INTERVAL_INTERSECTION TRIANGLE_SQUARE)

# --- Platform-specific settings ---
if (WIN32)
    # Apply MinGW static flags only when using GNU or Clang toolchains targeting Windows.
    # Use generator-expression so this won't affect MSVC.
    foreach(tgt ROTATING_SQUARE SPRITES SCREENER FRAC_TREE BUSY_TURTLE GAME_LIFE 3D GRAPH CYCLOID FLOODFILL INTERVAL_INTERSECTION TRIANGLE_SQUARE)
        if (TARGET ${tgt})
            target_link_options(${tgt} PRIVATE
                    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<PLATFORM_ID:Windows>>:
                    -static;-static-libgcc;-static-libstdc++;-Wl,-Bstatic;-lwinpthread;-Wl,-Bdynamic
                    >
            )
        endif()
    endforeach()

    # system libs (you can also add these per-target via target_link_libraries)
    link_libraries(opengl32 winmm gdi32)
elseif(APPLE)
    # macOS uses frameworks
    find_library(COREFOUNDATION_FRAMEWORK Foundation)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(CARBON_FRAMEWORK Carbon)

    link_libraries(
            "-framework OpenGL"
            ${COREFOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
            ${CARBON_FRAMEWORK}
    )
elseif(UNIX)
    # Linux: link X11, pthread, etc. if needed
    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT} GL X11)
endif()
