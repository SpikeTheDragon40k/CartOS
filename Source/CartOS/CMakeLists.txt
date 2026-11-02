cmake_minimum_required(VERSION 3.13)
project(CartOS VERSION 0.1.24 LANGUAGES CXX)

# Handheld builds use tools/miyoo-toolchain.cmake. A plain native build gives a
# desktop preview window that reads a copy of an SD card from CARTOS_SD_ROOT.
if(NOT CMAKE_CROSSCOMPILING)
    message(STATUS "Building the CartOS desktop preview (games and apps do not launch)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 QUIET CONFIG)
if(CMAKE_CROSSCOMPILING)
    # The Union sysroot also contains legacy libpng 1.2. The runtime artwork
    # loader uses libpng's simplified API, so select the 1.6 headers/library.
    find_path(LAUNCHER_PNG_INCLUDE_DIR png.h
        PATHS /usr/include/libpng16 NO_DEFAULT_PATH)
    find_library(LAUNCHER_PNG_LIBRARY NAMES png16
        PATHS /usr/lib NO_DEFAULT_PATH)
    if(NOT LAUNCHER_PNG_INCLUDE_DIR OR NOT LAUNCHER_PNG_LIBRARY)
        message(FATAL_ERROR "libpng16 headers and library are required in the Miyoo target sysroot")
    endif()
    add_library(PNG::PNG UNKNOWN IMPORTED)
    set_target_properties(PNG::PNG PROPERTIES
        IMPORTED_LOCATION "${LAUNCHER_PNG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LAUNCHER_PNG_INCLUDE_DIR}")
else()
    find_package(PNG REQUIRED)
endif()
if(NOT TARGET SDL2::SDL2)
    set(_sdl2_hints)
    if(DEFINED SDL2_ROOT)
        list(APPEND _sdl2_hints "${SDL2_ROOT}" "${SDL2_ROOT}/lib/cmake/SDL2")
    endif()
    find_path(SDL2_INCLUDE_DIR SDL.h
        HINTS ${_sdl2_hints}
        PATH_SUFFIXES include/SDL2 include SDL2
        NO_CMAKE_FIND_ROOT_PATH
        NO_DEFAULT_PATH)
    find_library(SDL2_LIBRARY
        NAMES SDL2 SDL2-2.0
        HINTS ${_sdl2_hints}
        PATH_SUFFIXES lib lib32
        NO_CMAKE_FIND_ROOT_PATH
        NO_DEFAULT_PATH)
    if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
        message(FATAL_ERROR "SDL2 headers/library not found. Set SDL2_ROOT to a prefix containing include/SDL2 and lib/ or lib32/.")
    endif()
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
endif()

if(CMAKE_CROSSCOMPILING)
    add_compile_definitions(LAUNCHER_DEVICE=1)
endif()

add_executable(cartos
    src/main.cpp
    src/launcher.cpp
    src/artwork.cpp
    src/draw.cpp
    src/platform.cpp
)
target_include_directories(cartos PRIVATE src)
target_compile_definitions(cartos PRIVATE CARTOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(cartos PRIVATE SDL2::SDL2 PNG::PNG)

# The build script creates CartOS's ABI-compatible helper in the target SDL2
# prefix before CMake runs. Link it so SDL2's imported scaler symbols resolve.
if(DEFINED SDL2_ROOT)
    find_library(MIYOO_NEON_LIBRARY
        NAMES neonarmmiyoo
        HINTS "${SDL2_ROOT}"
        PATH_SUFFIXES lib lib32
        NO_CMAKE_FIND_ROOT_PATH
        NO_DEFAULT_PATH)
    if(MIYOO_NEON_LIBRARY)
        target_link_libraries(cartos PRIVATE "${MIYOO_NEON_LIBRARY}")
    else()
        message(FATAL_ERROR "CartOS's Miyoo SDL2 compatibility library was not built in SDL2_ROOT")
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(cartos PRIVATE m dl)
endif()

install(TARGETS cartos RUNTIME DESTINATION bin)
