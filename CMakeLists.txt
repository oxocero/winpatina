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
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--subsystem,console:5.0")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

        # Use msvcrt.dll for maximum compatibility (not UCRT)
        # add_compile_options(-mcrt=msvcrt)  # Uncomment if targeting Win2000
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
    src/winpatina_detect.cpp
    src/winpatina_vt_parser.cpp
    src/winpatina_screen.cpp
    src/winpatina_colour.cpp
    src/winpatina_render.cpp
    src/winpatina_input.cpp
    src/winpatina_vt_input.cpp
    src/winpatina_query.cpp
    src/winpatina_utf8.cpp
    src/winpatina_main.cpp
)

set(WINPATINA_HEADERS
    include/winpatina.h
    src/internal.h
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
