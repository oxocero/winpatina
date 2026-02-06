cmake_minimum_required(VERSION 3.10)
project(winpatina
    VERSION 1.0.0
    DESCRIPTION "VT-to-Win32 Console translation layer for legacy Windows"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Architecture Configuration
# =============================================================================
#
# To build for different architectures:
#
# 64-bit (default with mingw64):
#   cmake -B build64 -G "MinGW Makefiles" \
#       -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe \
#       -DCMAKE_MAKE_PROGRAM=C:/mingw64/bin/mingw32-make.exe
#
# 32-bit (requires mingw32 toolchain):
#   cmake -B build32 -G "MinGW Makefiles" \
#       -DCMAKE_CXX_COMPILER=C:/mingw32/bin/g++.exe \
#       -DCMAKE_MAKE_PROGRAM=C:/mingw32/bin/mingw32-make.exe
#
# Or download mingw32 from: https://github.com/niXman/mingw-builds-binaries/releases
# Look for: i686-*-release-posix-dwarf-*.7z (32-bit)
#
# =============================================================================

# Detect architecture from compiler
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WINPATINA_ARCH "x64")
    message(STATUS "WinPatina: Building for 64-bit (x64)")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WINPATINA_ARCH "x86")
    message(STATUS "WinPatina: Building for 32-bit (x86)")
else()
    message(WARNING "WinPatina: Unknown architecture, assuming 64-bit")
    set(WINPATINA_ARCH "x64")
endif()

# =============================================================================
# Platform Configuration
# =============================================================================

if(WIN32)
    # Windows 2000 targeting (NT 5.0)
    add_compile_definitions(
        _WIN32_WINNT=0x0500
        WINVER=0x0500
        NTDDI_VERSION=0x05000000
        UNICODE
        _UNICODE
    )

    if(MINGW)
        # MinGW-specific flags for Windows 2000 compatibility
        # Note: subsystem 5.0 requires 32-bit; 64-bit Windows started with Vista (6.0)
        if(WINPATINA_ARCH STREQUAL "x86")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--subsystem,console:5.0")
        else()
            # 64-bit minimum is Windows XP x64 / Server 2003 (5.2)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--subsystem,console:5.2")
        endif()

        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    endif()

    if(MSVC)
        # MSVC-specific flags
        add_compile_options(/W4 /utf-8)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    endif()
else()
    message(FATAL_ERROR "WinPatina is a Windows-only project")
endif()

# =============================================================================
# Build Options
# =============================================================================

option(WINPATINA_BUILD_TESTS "Build test suite" ON)
option(WINPATINA_BUILD_EXAMPLES "Build example applications" ON)
option(WINPATINA_USE_CIE_LAB "Use CIE Lab colour space for better colour matching" OFF)

if(WINPATINA_USE_CIE_LAB)
    add_compile_definitions(WINPATINA_USE_CIE_LAB=1)
endif()

# =============================================================================
# Source Files
# =============================================================================

set(WINPATINA_SOURCES
    # Core (implemented)
    src/winpatina_main.cpp
    src/winpatina_detect.cpp

    # Phase 1
    src/winpatina_vt_parser.cpp
    # src/winpatina_screen.cpp
    # src/winpatina_colour.cpp
    # src/winpatina_render.cpp

    # Phase 2 (not yet implemented)
    # src/winpatina_input.cpp
    # src/winpatina_vt_input.cpp
    # src/winpatina_query.cpp
    # src/winpatina_utf8.cpp
)

set(WINPATINA_HEADERS
    include/winpatina.h
    src/internal.h
    src/winpatina_vt_parser.h
)

# =============================================================================
# Library Target
# =============================================================================

add_library(winpatina_lib STATIC ${WINPATINA_SOURCES} ${WINPATINA_HEADERS})

target_include_directories(winpatina_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link Windows libraries
target_link_libraries(winpatina_lib PRIVATE kernel32 user32)

# =============================================================================
# Executable Target
# =============================================================================

add_executable(winpatina src/main.cpp)
target_link_libraries(winpatina PRIVATE winpatina_lib)

# Add architecture suffix to output names
set_target_properties(winpatina PROPERTIES
    OUTPUT_NAME "winpatina-${WINPATINA_ARCH}"
)
set_target_properties(winpatina_lib PROPERTIES
    OUTPUT_NAME "winpatina_lib-${WINPATINA_ARCH}"
)

# =============================================================================
# Tests
# =============================================================================

if(WINPATINA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Examples
# =============================================================================

if(WINPATINA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS winpatina winpatina_lib
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES include/winpatina.h DESTINATION include)
