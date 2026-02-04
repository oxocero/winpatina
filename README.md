# WinPatina

**VT-to-Win32 Console translation layer for legacy Windows**

WinPatina enables modern terminal applications that emit VT/ANSI escape sequences to run on legacy Windows systems (Windows 2000 through early Windows 10) that lack native Virtual Terminal Processing support.

## Features

- **Dual-mode operation**: Automatically detects terminal capabilities and operates in either:
  - **VT Passthrough**: For modern terminals (Windows Terminal, ConEmu) — minimal overhead
  - **Win32 Translation**: For legacy consoles — full VT to Win32 API translation

- **Comprehensive VT support**:
  - Cursor positioning and visibility
  - Text attributes (bold, underline, reverse)
  - 16/256/RGB colours (with quantisation to 16 on legacy)
  - Alternate screen buffer
  - Mouse input (SGR 1006 format)
  - Keyboard input translation

- **Wide compatibility**:
  - Minimum: Windows 2000
  - Maximum: Windows 11 (passthrough mode)
  - Tested with: notcurses, ncurses, termina

## Building

### Requirements

- CMake 3.10+
- C++17 compiler (MinGW-w64 8+)

### Build Commands

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Windows 2000 Targeting (MinGW)

For maximum compatibility with legacy Windows:

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64.cmake
```

## Usage

### Command Line

```bash
# Run a command through WinPatina
winpatina cmd.exe

# Show system capabilities
winpatina --info

# Force translation mode (for testing)
winpatina --force myapp.exe
```

### Library API

```c
#include <winpatina.h>

int main() {
    // Query system capabilities
    WinPatinaSystemInfo info;
    wp_query_system(&info);

    // Create instance with default config
    WinPatina* wp = wp_create(NULL);

    // Spawn child process
    wp_spawn(wp, "myapp.exe", NULL);

    // Run until child exits
    int exit_code = wp_run(wp);

    // Clean up
    wp_destroy(wp);

    return exit_code;
}
```

## Architecture

```
┌───────────────────────────────────────────────────────────┐
│  winpatina.exe                                            │
├───────────────────────────────────────────────────────────┤
│  Capability Detector → VT Passthrough / Win32 Translation │
├───────────────────────────────────────────────────────────┤
│  VT Parser → Screen Buffer → Win32 Renderer               │
├───────────────────────────────────────────────────────────┤
│  Input Handler → VT Input Generator                       │
└───────────────────────────────────────────────────────────┘
```

## Limitations

- **No italic/strikethrough**: Win32 Console doesn't support these
- **16 colours on legacy**: RGB/256 colours quantised to 16-colour palette
- **No graphics protocols**: Sixel, Kitty, iTerm2 images not supported
- **Underline Vista+ only**: COMMON_LVB_UNDERSCORE requires Vista

## Licence

MIT Licence

## Acknowledgements

- **ncurses** and **PDCurses** for Win32 Console API patterns
- **notcurses** for VT sequence reference
- **Paul Williams** for the VT parser state machine specification
