# Termina Library Analysis for Legacy Windows Support

**Date:** 3 February 2026
**Purpose:** Analyse termina library dependencies and features to assess requirements for **WinPatina** - a legacy Windows compatibility layer

---

## Executive Summary

Termina is a cross-platform Terminal UI engine written in C11 with a stable ABI. It heavily relies on modern terminal capabilities, particularly **Virtual Terminal Processing** (VT/ANSI escape sequences) which was introduced in Windows 10 Version 1511 (November 2015). Legacy Windows versions (Windows 7, 8, 8.1, and early Windows 10 builds) lack native VT sequence support, making direct compatibility impossible without an abstraction layer.

This document catalogues the terminal features termina requires and specifies **WinPatina** - a VT-to-Win32 Console translation layer that enables termina (and compatible libraries like ncurses and notcurses) to run on legacy Windows systems, targeting **Windows 2000** as the minimum supported version.

---

## 1. Core Platform Dependencies

### 1.1 Windows API Usage (Current Implementation)

The library's Windows backend (`src/platform/platform_win.c`) uses:

| API | Purpose | Legacy Availability |
|-----|---------|---------------------|
| `GetStdHandle()` | Obtain console handles | Available (Win2000+) |
| `SetConsoleMode()` | Configure console behaviour | Available, but VT flag missing |
| `ReadConsoleInput()` | Raw input events | Available |
| `SetConsoleCP/SetConsoleOutputCP(65001)` | UTF-8 codepage | Available (XP+) |
| `GetConsoleWindow()` | Window handle for sizing | Available |
| `GetClientRect()` | Pixel dimensions | Available |
| `ENABLE_VIRTUAL_TERMINAL_PROCESSING` | VT sequence support | **Windows 10 1511+ only** |
| `ENABLE_VIRTUAL_TERMINAL_INPUT` | VT input parsing | **Windows 10 1511+ only** |

### 1.2 Critical Missing Feature on Legacy Windows

The `ENABLE_VIRTUAL_TERMINAL_PROCESSING` flag (value `0x0004`) is the **single most critical dependency**. Without it, Windows Console (conhost.exe) interprets escape sequences as literal characters rather than control codes.

---

## 2. ANSI/VT Escape Sequences Required

Termina uses extensive VT escape sequences. WinPatina must translate these to Win32 Console API calls.

### 2.1 Screen Management

| Sequence | Function | Win32 Equivalent |
|----------|----------|------------------|
| `\x1b[?1049h` | Enter alternate screen buffer | `CreateConsoleScreenBuffer()` + `SetConsoleActiveScreenBuffer()` |
| `\x1b[?1049l` | Exit alternate screen buffer | Restore original buffer |
| `\x1b[2J` | Clear entire screen | `FillConsoleOutputCharacter()` + `FillConsoleOutputAttribute()` |
| `\x1b[?2026h/l` | Synchronised output (DCS) | Buffer writes, flush atomically |

### 2.2 Cursor Control

| Sequence | Function | Win32 Equivalent |
|----------|----------|------------------|
| `\x1b[Y;XH` | Set cursor position | `SetConsoleCursorPosition()` |
| `\x1b[?25h` | Show cursor | `SetConsoleCursorInfo()` with `bVisible = TRUE` |
| `\x1b[?25l` | Hide cursor | `SetConsoleCursorInfo()` with `bVisible = FALSE` |

### 2.3 Text Styling (SGR - Select Graphic Rendition)

| Sequence | Function | Win32 Equivalent |
|----------|----------|------------------|
| `\x1b[0m` | Reset attributes | `FOREGROUND_RED\|GREEN\|BLUE` default |
| `\x1b[1m` | Bold/bright | `FOREGROUND_INTENSITY` |
| `\x1b[3m` | Italic | **Not supported** - ignore or underline |
| `\x1b[4m` | Underline | `COMMON_LVB_UNDERSCORE` (Vista+) |
| `\x1b[7m` | Inverse/reverse | Swap foreground/background |
| `\x1b[30-37m` | Foreground colour (basic) | Map to `FOREGROUND_*` flags |
| `\x1b[40-47m` | Background colour (basic) | Map to `BACKGROUND_*` flags |
| `\x1b[90-97m` | Bright foreground | Add `FOREGROUND_INTENSITY` |
| `\x1b[100-107m` | Bright background | Add `BACKGROUND_INTENSITY` |

### 2.4 Extended Colour Modes

| Sequence | Function | Legacy Support |
|----------|----------|----------------|
| `\x1b[38;5;Nm` | 256-colour foreground | Requires palette mapping to 16 colours |
| `\x1b[48;5;Nm` | 256-colour background | Requires palette mapping to 16 colours |
| `\x1b[38;2;R;G;Bm` | Truecolor foreground | Requires palette mapping to 16 colours |
| `\x1b[48;2;R;G;Bm` | Truecolor background | Requires palette mapping to 16 colours |

**Note:** Legacy Windows Console only supports 16 colours. A colour quantisation algorithm is essential.

### 2.5 Mouse Input Sequences

| Sequence | Function | Win32 Equivalent |
|----------|----------|------------------|
| `\x1b[?1002h` | Enable button tracking | `ENABLE_MOUSE_INPUT` in console mode |
| `\x1b[?1006h` | SGR extended mouse format | Parse `MOUSE_EVENT_RECORD` from `ReadConsoleInput()` |

**Good news:** Windows Console has native mouse support via `INPUT_RECORD` structures. WinPatina converts these to xterm-compatible escape sequences that termina expects.

---

## 3. Terminal Capability Detection

Termina performs capability detection during initialisation. The library queries the terminal identity using:

| Query | Sequence | Purpose |
|-------|----------|---------|
| XTVERSION | `\x1b[>0q` | Terminal name/version |
| XTGETTCAP | `\x1bP+q544e\x1b\\` | Terminal name capability |
| TDA | `\x1b[=c` | Tertiary device attributes |
| SDA | `\x1b[>c` | Secondary device attributes |
| DA1 | `\x1b[c` | Primary device attributes (sentinel) |

WinPatina would need to:
1. Intercept these queries before they reach the console
2. Return appropriate responses identifying as a compatible terminal
3. Report accurate capability flags (no truecolor, no Sixel, etc.)

---

## 4. Image Protocol Support

Termina supports three image protocols:

| Protocol | Sequences | Legacy Feasibility |
|----------|-----------|-------------------|
| Sixel | DCS graphics bands | **Not feasible** - requires VT340+ emulation |
| Kitty Graphics | APC chunks | **Not feasible** - modern protocol |
| iTerm2 Inline | OSC 1337 | **Not feasible** - modern protocol |

**Recommendation:** Disable image protocols entirely on legacy Windows. Termina already has symbol-based fallback rendering using Unicode characters.

---

## 5. WinPatina Architecture

### 5.1 Approach Options

| Approach | Description | Complexity | Performance |
|----------|-------------|------------|-------------|
| **A: Output Filter** | Intercept stdout, parse sequences, call Win32 API | Medium | Good |
| **B: Console Driver** | Replace conhost.exe behaviour | Very High | Excellent |
| **C: Pseudo-Terminal** | Create PTY layer (ConPTY-like) | High | Good |
| **D: Direct Integration** | Modify termina's platform layer | Medium | Excellent |

**Recommended approach: A (Output Filter) with D (Direct Integration)**

WinPatina implements a new platform backend (`platform_winpatina.c`) that:
1. Detects Windows version at startup
2. Falls back to Win32 Console API when VT processing unavailable
3. Maintains an internal screen buffer for efficient diff-based updates

### 5.2 Component Requirements

```
winpatina/
├── src/
│   ├── winpatina_detect.c   # Windows version detection
│   ├── winpatina_screen.c   # Screen buffer management
│   ├── winpatina_cursor.c   # Cursor positioning
│   ├── winpatina_colour.c   # Colour quantisation (RGB → 16)
│   ├── winpatina_input.c    # Input event translation
│   ├── winpatina_mouse.c    # Mouse event handling
│   └── winpatina_vt.c       # Escape sequence parser
├── include/
│   └── winpatina.h          # Public API
└── CMakeLists.txt
```

### 5.3 WinPatina Implementation Challenges

#### 5.3.1 Colour Quantisation
Converting 24-bit RGB or 256-colour palette to 16 colours requires:
- Perceptual colour distance calculation (CIE Lab recommended)
- Dithering consideration (may cause flickering on updates)
- Palette mapping table for 256→16 conversion

#### 5.3.2 Screen Buffer Synchronisation
Legacy Console lacks synchronised output. To prevent tearing:
- Maintain shadow buffer
- Calculate minimal diff
- Write changes as single `WriteConsoleOutput()` call

#### 5.3.3 Escape Sequence Parser
Need a robust state machine to parse:
- CSI sequences: `\x1b[...m`, `\x1b[...H`, etc.
- OSC sequences: `\x1b]...ST`
- DCS sequences: `\x1bP...ST` (can be ignored for legacy)

#### 5.3.4 UTF-8 and Wide Characters
Legacy Console has inconsistent Unicode support:
- Use `WriteConsoleW()` with UTF-16 conversion
- Handle double-width CJK characters carefully
- Emoji support limited (may display as replacement characters)

---

## 6. Feature Compatibility Matrix

| Feature | Windows 10+ | WinPatina (Win2000+) | Notes |
|---------|-------------|----------------------|-------|
| Basic text output | Full | Full | `WriteConsoleOutputW()` |
| 16 ANSI colours | Full | Full | All versions |
| 256 colours | Full | Degraded (mapped to 16) | Quantisation required |
| Truecolor (16.7M) | Full | Degraded (mapped to 16) | Quantisation required |
| Bold text | Full | Full | `FOREGROUND_INTENSITY` |
| Italic text | Full | Not supported | No Win32 equivalent |
| Underline | Full | Vista+ only | `COMMON_LVB_UNDERSCORE` |
| Cursor control | Full | Full | All versions |
| Alternate screen | Full | Full | `CreateConsoleScreenBuffer()` |
| Mouse input | Full | Full | All versions |
| Keyboard input | Full | Full | All versions |
| UTF-8 codepage | Full | XP+ only | Win2000 needs manual UTF-8↔UTF-16 |
| Wide characters | Full | Full | Via `*W()` functions |
| Emoji | Full | Limited | Font/rendering dependent |
| Sixel graphics | Partial | Not supported | — |
| Kitty graphics | Partial | Not supported | — |
| Synchronised output | Full | Emulated | Buffered writes |

---

## 7. Windows Version Detection

```cpp
#include <windows.h>

enum class WinPatinaTarget {
    WIN_2000,       // Windows 2000 (NT 5.0)
    WIN_XP,         // Windows XP (NT 5.1)
    WIN_VISTA,      // Windows Vista (NT 6.0)
    WIN_7,          // Windows 7 (NT 6.1)
    WIN_8,          // Windows 8/8.1 (NT 6.2/6.3)
    WIN_10_PRE_VT,  // Windows 10 < 1511
    WIN_10_VT,      // Windows 10 >= 1511 (VT support)
    WIN_11          // Windows 11
};

WinPatinaTarget detect_windows_version() {
    // First, check if VT processing is available
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
        if (SetConsoleMode(hOut, mode | 0x0004)) {
            SetConsoleMode(hOut, mode); // Restore
            return WinPatinaTarget::WIN_10_VT;
        }
    }

    // Fallback to version check via GetVersionEx (deprecated but works on Win2000+)
    OSVERSIONINFOW osvi = { sizeof(osvi) };
    #pragma warning(suppress: 4996) // GetVersionExW deprecated
    GetVersionExW(&osvi);

    if (osvi.dwMajorVersion >= 10) return WinPatinaTarget::WIN_10_PRE_VT;
    if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion >= 2) return WinPatinaTarget::WIN_8;
    if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) return WinPatinaTarget::WIN_7;
    if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0) return WinPatinaTarget::WIN_VISTA;
    if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion >= 1) return WinPatinaTarget::WIN_XP;

    return WinPatinaTarget::WIN_2000;
}

// Check if UTF-8 codepage is available (Windows XP+)
bool has_utf8_codepage() {
    return IsValidCodePage(65001);
}
```

**Note:** `versionhelpers.h` is not available on Windows 2000/XP, so WinPatina uses the older `GetVersionExW()` API for maximum compatibility.

---

## 7.1 Building WinPatina for Windows 2000

WinPatina targets Windows 2000 (NT 5.0) as the minimum supported version using MinGW-w64 with GCC:

```bash
# Compiler flags for Windows 2000 targeting
CXXFLAGS = -std=c++17 \
           -D_WIN32_WINNT=0x0500 \
           -DWINVER=0x0500 \
           -DNTDDI_VERSION=0x05000000 \
           -mcrt=msvcrt

LDFLAGS = -Wl,--subsystem,console:5.0 \
          -static-libgcc \
          -static-libstdc++
```

**Key considerations:**

| Concern | Solution |
|---------|----------|
| C runtime | Use `msvcrt.dll` (`-mcrt=msvcrt`), not UCRT |
| Subsystem version | Set to 5.0 for Win2000 (`--subsystem,console:5.0`) |
| UTF-8 codepage | Not available on Win2000; handle UTF-8↔UTF-16 conversion internally |
| API availability | Check MSDN "Minimum supported client" for each function |
| Static linking | Link libgcc/libstdc++ statically to avoid runtime DLL issues |
| `versionhelpers.h` | Don't use; use `GetVersionExW()` instead |

**UTF-8 Handling on Windows 2000:**

Since codepage 65001 (UTF-8) is not available on Windows 2000, WinPatina must:
1. Accept UTF-8 input from the application
2. Convert internally to UTF-16 using `MultiByteToWideChar()` with `CP_UTF8`
3. Use wide (`*W()`) Console API functions exclusively
4. Convert UTF-16 back to UTF-8 for any output to the application

```cpp
// Internal UTF-8 to UTF-16 conversion (works on Win2000)
std::wstring utf8_to_utf16(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                   static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), result.data(), len);
    return result;
}
```

**Note:** `CP_UTF8` (65001) is recognised by `MultiByteToWideChar()` on Windows 2000 even though the console doesn't support it as an active codepage.

---

## 8. Minimum Win32 Console API Surface

WinPatina requires these Console API functions:

### Output
- `GetStdHandle(STD_OUTPUT_HANDLE)`
- `CreateConsoleScreenBuffer()`
- `SetConsoleActiveScreenBuffer()`
- `WriteConsoleOutputW()` / `WriteConsoleW()`
- `FillConsoleOutputCharacterW()`
- `FillConsoleOutputAttribute()`
- `SetConsoleCursorPosition()`
- `SetConsoleCursorInfo()`
- `GetConsoleScreenBufferInfo()`
- `SetConsoleTextAttribute()`

### Input
- `GetStdHandle(STD_INPUT_HANDLE)`
- `SetConsoleMode()`
- `GetNumberOfConsoleInputEvents()`
- `ReadConsoleInputW()`
- `PeekConsoleInputW()`

### Window
- `GetConsoleWindow()`
- `SetConsoleTitle()`

---

## 9. WinPatina Estimated Implementation Effort

| Component | Complexity | Lines of Code (est.) |
|-----------|------------|---------------------|
| `winpatina_detect.cpp` | Low | ~120 |
| `winpatina_vt.cpp` | Medium-High | ~850 |
| `winpatina_screen.cpp` | Medium | ~450 |
| `winpatina_colour.cpp` | Medium | ~300 |
| `winpatina_cursor.cpp` | Low | ~200 |
| `winpatina_mouse.cpp` | Low | ~150 |
| `winpatina_input.cpp` | Low | ~150 |
| `winpatina_utf8.cpp` | Low | ~150 |
| Public API (`winpatina.h`) | Low | ~130 |
| **Total** | | **~2,500** |

**Language:** C++17 with `extern "C"` public API for maximum compatibility with C consumers.

---

## 10. Alternatives to WinPatina

### 10.1 Use Existing Terminal Emulator
Rather than building a compatibility layer, require users to run termina applications inside:
- **mintty** (Cygwin/MSYS2 terminal) - full VT support
- **ConEmu** - VT emulation layer
- **Cmder** - ConEmu-based

**Pros:** No development effort, full feature support
**Cons:** Requires additional software installation

### 10.2 PDCurses/ncurses Compatibility Layer
Use PDCurses (Public Domain Curses) as an intermediary:
- PDCurses has mature Win32 Console support
- Would require significant termina architecture changes

**Pros:** Battle-tested, handles edge cases
**Cons:** Adds dependency, performance overhead, architectural mismatch

### 10.3 Windows Console Replacement
Ship a custom console host (replacement for conhost.exe):
- Similar to Windows Terminal's approach
- Full control over rendering

**Pros:** Complete feature parity possible
**Cons:** Massive undertaking, security considerations

---

## 11. WinPatina Recommendations

### For Minimal Viable Product (MVP)
1. Implement basic VT parser in `winpatina_vt.c` for CSI sequences only
2. Support 16-colour mode with automatic degradation
3. Implement alternate screen buffer via `CreateConsoleScreenBuffer()`
4. Translate mouse events from `INPUT_RECORD` to xterm format
5. Use `WriteConsoleOutputW()` for atomic screen updates

### For Production Quality WinPatina
1. Add robust 256→16 and RGB→16 colour mapping in `winpatina_colour.c`
2. Implement proper Unicode handling with fallback glyphs
3. Add configurable capability reporting
4. Consider optional third-party terminal recommendation as fallback

### Not Recommended for WinPatina
- Sixel/Kitty/iTerm2 image protocol support (impractical)
- Italic text emulation (no standard representation)
- Full VT100/VT220 emulation (scope creep)

---

## 12. WinPatina Conclusion

Building WinPatina is **feasible but non-trivial**. The core challenge is replacing modern VT sequence processing with equivalent Win32 Console API calls. Targeting Windows 2000 as the minimum adds complexity around UTF-8 handling (no codepage 65001) but is achievable. A focused implementation targeting the essential features (text output, colours, cursor control, mouse input) could be achieved in approximately 2,500 lines of C++ code.

The main trade-offs with WinPatina will be:
- **Colour fidelity:** 16.7M colours → 16 colours
- **Text styling:** No italic support, limited underline
- **Graphics:** No image protocol support
- **Unicode:** Partial support depending on system fonts

For users on legacy Windows who need full termina features, recommending a modern terminal emulator (mintty, ConEmu) may be a more practical alternative to WinPatina.

---

## Appendix A: Win32 Console Colour Mapping

```c
// ANSI colour index to Win32 attribute mapping
static const WORD ansi_to_win32_fg[16] = {
    0,                                          // 0: Black
    FOREGROUND_RED,                             // 1: Red
    FOREGROUND_GREEN,                           // 2: Green
    FOREGROUND_RED | FOREGROUND_GREEN,          // 3: Yellow
    FOREGROUND_BLUE,                            // 4: Blue
    FOREGROUND_RED | FOREGROUND_BLUE,           // 5: Magenta
    FOREGROUND_GREEN | FOREGROUND_BLUE,         // 6: Cyan
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,  // 7: White
    FOREGROUND_INTENSITY,                       // 8: Bright Black (Grey)
    FOREGROUND_RED | FOREGROUND_INTENSITY,      // 9: Bright Red
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,    // 10: Bright Green
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,  // 11: Bright Yellow
    FOREGROUND_BLUE | FOREGROUND_INTENSITY,     // 12: Bright Blue
    FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,   // 13: Bright Magenta
    FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY, // 14: Bright Cyan
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY  // 15: Bright White
};
```

## Appendix B: Useful References

- [Microsoft Console Virtual Terminal Sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences)
- [Windows Console API Reference](https://learn.microsoft.com/en-us/windows/console/console-functions)
- [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
- [ECMA-48 (ANSI Escape Codes)](https://www.ecma-international.org/publications-and-standards/standards/ecma-48/)

---

## Appendix C: ncurses Compatibility Analysis (Full Support)

ncurses 6.6 provides **full legacy Windows support** through its terminal driver architecture. Unlike termina's VT-centric approach, ncurses has a dedicated Windows Console driver that bypasses VT sequences entirely on legacy systems.

### C.1 ncurses Windows Architecture

ncurses uses a pluggable terminal driver system (`--enable-term-driver`) with two drivers:
- **terminfo driver** - Standard Unix terminal handling via terminfo database
- **win32con driver** - Direct Windows Console API integration

The win32con driver (`ncurses/win32con/win32_driver.c`) renders directly to the Windows Console without requiring VT sequence support.

### C.2 Building ncurses for Legacy Windows

ncurses builds natively on Windows via MinGW/MSYS2:

```bash
# MinGW-w64 UCRT build (recommended)
pacman -S mingw-w64-ucrt-x86_64-toolchain

# Configure for Windows Console driver
./configure \
    --enable-term-driver \
    --enable-sp-funcs \
    --enable-widec \
    --with-pcre2 \
    --prefix=/mingw64

make && make install
```

**Key configure options:**
- `--enable-term-driver` - **Required** for Windows, enables pluggable driver architecture
- `--enable-sp-funcs` - Screen pointer functions for multi-terminal support
- `--with-pcre2` - Regex support (POSIX regex unavailable on MinGW)
- `--enable-widec` - Wide character (Unicode) support

### C.3 ncurses Win32 Console Driver Features

The `win32_driver.c` implementation provides:

| Feature | Implementation | Legacy Support |
|---------|---------------|----------------|
| Screen output | `WriteConsoleOutputW()` | Full |
| Cursor control | `SetConsoleCursorPosition()` | Full |
| Colour (16) | `SetConsoleTextAttribute()` | Full |
| Colour (256) | Palette mapping | Degraded |
| Bold | `FOREGROUND_INTENSITY` | Full |
| Reverse | Attribute swap | Full |
| Underline | `COMMON_LVB_UNDERSCORE` | Vista+ |
| Mouse | `ReadConsoleInput()` + `MOUSE_EVENT_RECORD` | Full |
| Keyboard | `ReadConsoleInput()` + `KEY_EVENT_RECORD` | Full |
| Alternate screen | `CreateConsoleScreenBuffer()` | Full |
| Window resize | `WINDOW_BUFFER_SIZE_EVENT` | Full |
| Unicode | UTF-16 via `*W()` functions | Full |

### C.4 ncurses Colour Mapping

The win32con driver maps ncurses colour pairs to Win32 attributes:

```c
// From ncurses/win32con/win32_driver.c
static WORD MapAttr(WORD res, attr_t ch) {
    if (ch & A_COLOR) {
        int p = PairNumber(ch);
        if (p > 0 && p < CON_NUMPAIRS) {
            WORD a = WINCONSOLE.pairs[p];
            res = (WORD)((res & 0xff00) | a);
        }
    }
    if (ch & A_REVERSE) res = RevAttr(res);
    if (ch & A_STANDOUT) res = RevAttr(res) | BACKGROUND_INTENSITY;
    if (ch & A_BOLD) res |= FOREGROUND_INTENSITY;
    if (ch & A_DIM) res |= BACKGROUND_INTENSITY;
    return res;
}
```

### C.5 ncurses Legacy Windows Compatibility

| Windows Version | ncurses Support | Notes |
|-----------------|-----------------|-------|
| Windows 2000 | Full | All core features work |
| Windows XP | Full | All core features work |
| Windows Vista | Full | All core features work |
| Windows 7 | Full | All core features work |
| Windows 8/8.1 | Full | All core features work |
| Windows 10 (pre-1511) | Full | All core features work |
| Windows 10 (1511+) | Full | Can use VT mode if preferred |
| Windows 11 | Full | All features work |

**ncurses advantage:** The win32con driver works on **all Windows versions** back to Windows 2000, making it an excellent reference for WinPatina's implementation.

### C.6 Integrating ncurses with WinPatina

To provide full ncurses compatibility for termina via WinPatina:

1. **Option A: ncurses backend** - Implement WinPatina's platform layer atop ncurses
   - Pros: Proven, handles all edge cases
   - Cons: Adds ncurses as dependency (~500KB)

2. **Option B: Port win32con driver** - Extract and adapt ncurses' Win32 code into WinPatina
   - Pros: No runtime dependency
   - Cons: Maintenance burden, licensing considerations (MIT-X11)

3. **Option C: Reference implementation** - Use ncurses driver as specification for WinPatina
   - Pros: Clean-room implementation, no licensing issues
   - Cons: Development time

**Recommended:** Option A for rapid WinPatina development, Option C for production.

---

## Appendix D: notcurses Compatibility Analysis (Maximal Support)

notcurses has more stringent requirements than ncurses, as it was designed for modern terminals with advanced capabilities. However, significant functionality can still be achieved on legacy Windows.

### D.1 notcurses Windows Requirements

notcurses requires **Windows ConPTY** (Console Pseudo-Terminal), introduced in Windows 10 Version 1809 (October 2018). From `src/lib/windows.c`:

```c
// notcurses Windows initialization
if(!SetConsoleMode(ti->outhandle, ENABLE_PROCESSED_OUTPUT
                   | ENABLE_WRAP_AT_EOL_OUTPUT
                   | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                   | DISABLE_NEWLINE_AUTO_RETURN
                   | ENABLE_LVB_GRID_WORLDWIDE)){
    logerror("couldn't set output console mode");
    return -1;
}
loginfo("verified Windows ConPTY");
```

**Critical flags:**
- `ENABLE_VIRTUAL_TERMINAL_PROCESSING` (0x0004) - VT sequence processing
- `ENABLE_VIRTUAL_TERMINAL_INPUT` (0x0200) - VT input parsing
- `DISABLE_NEWLINE_AUTO_RETURN` (0x0008) - Proper cursor movement
- `ENABLE_LVB_GRID_WORLDWIDE` (0x0010) - Unicode grid support

### D.2 notcurses Feature Requirements

| Feature | Requirement | Legacy Windows |
|---------|-------------|----------------|
| Terminfo database | Required | Available via ncurses/MinGW |
| libunistring | Required | Available via MinGW |
| VT sequence output | Required | **Not available** pre-1511 |
| ConPTY | Required for full support | **Windows 10 1809+** |
| 24-bit colour | Preferred | Not available |
| Sixel graphics | Optional | Not available |
| Kitty graphics | Optional | Not available |
| UTF-8 locale | Required | Available via codepage 65001 |

### D.3 notcurses Escape Sequences (Windows)

notcurses pre-defines VT sequences for Windows in `src/lib/windows.c`:

```c
const struct wtermdesc wterms[] = {
    { ESCAPE_CUP,   "\x1b[%i%p1%d;%p2%dH", },  // Cursor position
    { ESCAPE_CLEAR, "\x1b[2J", },               // Clear screen
    { ESCAPE_SMCUP, "\x1b[?1049h", },           // Alternate screen
    { ESCAPE_RMCUP, "\x1b[?1049l", },           // Exit alternate screen
    { ESCAPE_SETAF, "\x1b[38;5;%i%p1%dm", },    // 256-colour foreground
    { ESCAPE_SETAB, "\x1b[48;5;%i%p1%dm", },    // 256-colour background
    { ESCAPE_CIVIS, "\x1b[?25l", },             // Hide cursor
    { ESCAPE_CNORM, "\x1b[?25h", },             // Show cursor
    { ESCAPE_BOLD,  "\x1b[1m", },               // Bold
    { ESCAPE_SITM,  "\x1b[3m", },               // Italic
    { ESCAPE_SMUL,  "\x1b[4m", },               // Underline
    { ESCAPE_SGR0,  "\x1b[0m", },               // Reset
    // ... etc
};
```

### D.4 Maximal notcurses Support via WinPatina

To achieve maximum notcurses compatibility on legacy Windows, WinPatina intercepts VT sequences and translates them to Win32 Console API:

| notcurses Feature | WinPatina Implementation | Support Level |
|-------------------|----------------------|---------------|
| Text rendering | `WriteConsoleOutputW()` | Full |
| 256 colours | Quantise to 16 colours | Degraded |
| RGB colours | Quantise to 16 colours | Degraded |
| Cursor positioning | `SetConsoleCursorPosition()` | Full |
| Cursor visibility | `SetConsoleCursorInfo()` | Full |
| Bold text | `FOREGROUND_INTENSITY` | Full |
| Italic text | Ignore or underline fallback | Degraded |
| Underline | `COMMON_LVB_UNDERSCORE` | Vista+ |
| Alternate screen | `CreateConsoleScreenBuffer()` | Full |
| ncplanes | Software compositing | Full |
| Mouse input | `ReadConsoleInput()` conversion | Full |
| Keyboard input | `ReadConsoleInput()` conversion | Full |
| Widgets | All work (no VT dependency) | Full |
| Images (Sixel) | Disable / symbol fallback | Not supported |
| Images (Kitty) | Disable / symbol fallback | Not supported |
| Video playback | Disable | Not supported |

### D.5 notcurses Legacy Compatibility Matrix

| Windows Version | Maximum Support | Notes |
|-----------------|-----------------|-------|
| Windows 2000 | Partial | With WinPatina, no UTF-8 codepage |
| Windows XP | Partial | With WinPatina |
| Windows Vista | Partial | With WinPatina, underline support |
| Windows 7 | Partial | With WinPatina |
| Windows 8/8.1 | Partial | With WinPatina |
| Windows 10 < 1511 | Partial | With WinPatina |
| Windows 10 1511-1809 | Good | VT works, ConPTY missing |
| Windows 10 1809+ | Full | Native support (WinPatina not needed) |
| Windows 11 | Full | Native support (WinPatina not needed) |

### D.6 notcurses Terminal Detection

notcurses identifies terminals via environment variables and escape sequence responses:

```c
// Windows Terminal detection
const char* tp = getenv("TERM_PROGRAM");
if(tp){
    if(strcmp(tp, "mintty") == 0){
        ti->qterm = TERMINAL_MINTTY;
        return 0;
    }
}
ti->qterm = TERMINAL_MSTERMINAL;  // Default for Windows
```

WinPatina should:
1. Set appropriate `TERM_PROGRAM` environment variable
2. Respond to DA1 queries with appropriate terminal identification
3. Report capabilities accurately (no RGB, no Sixel, etc.)

### D.7 Recommended notcurses Strategy with WinPatina

**For Windows 7/8/8.1 and early Windows 10:**

1. **Use notcurses-core** (`notcurses_core_init()`) - minimal dependencies
2. **Enable WinPatina** (~3,000 lines) covering:
   - CSI sequences (cursor, colour, attributes)
   - Alternate screen buffer
   - Mouse/keyboard input translation
3. **Disable image protocols** - use `NCBLIT_1x1` or `NCBLIT_2x1` blitters
4. **Force 16-colour mode** via terminfo override
5. **Test with `notcurses-info`** to verify capability detection

**Alternative:** Require users to run inside mintty (MSYS2/Cygwin), which provides full VT support and ConPTY emulation on legacy Windows without needing WinPatina.

### D.8 notcurses vs ncurses for Legacy Windows

| Aspect | ncurses | notcurses |
|--------|---------|-----------|
| Legacy Windows support | Native | Requires emulation |
| 24-bit colour | Limited | Native (where supported) |
| Image protocols | None | Sixel, Kitty, iTerm2 |
| Unicode handling | Good | Excellent |
| Thread safety | Limited | Designed for threads |
| Widget library | Separate (forms, menus, panels) | Integrated |
| API complexity | Lower | Higher |
| Binary size | ~500KB | ~2MB with multimedia |
| Minimum Windows | Windows 2000 | Windows 10 1809 (native) |

**Recommendation for WinPatina:**
- Use **ncurses' win32con driver** as reference for the primary rendering backend
- Implement a **termina-to-WinPatina adapter** for the platform layer
- For notcurses compatibility, WinPatina provides the **VT-to-Win32 translation layer**

---

## Appendix E: WinPatina Combined Implementation Strategy

For maximum compatibility across termina, ncurses, and notcurses on legacy Windows:

### E.1 WinPatina Layered Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Application                         │
├─────────────────────────────────────────────────────┤
│     termina API  │  ncurses API  │  notcurses API   │
├─────────────────────────────────────────────────────┤
│              Unified Platform Layer                  │
├─────────────────┬───────────────────────────────────┤
│   VT Backend    │         WinPatina                 │
│  (Win10 1511+)  │    (Legacy Windows)               │
├─────────────────┴───────────────────────────────────┤
│                Windows Console API                   │
└─────────────────────────────────────────────────────┘
```

### E.2 WinPatina Feature Support Summary

| Feature | WinPatina (Win2000+) | VT (Win10 1511+) |
|---------|--------------|-------------|
| Text output | Full | Full |
| 16 colours | Full | Full |
| 256 colours | Mapped to 16 | Full |
| Truecolor | Mapped to 16 | Full |
| Bold | Full | Full |
| Italic | No | Full |
| Underline | Vista+ | Full |
| Cursor | Full | Full |
| Mouse | Full | Full |
| Keyboard | Full | Full |
| Alternate screen | Full | Full |
| Unicode | Full (UTF-16) | Full |
| Sixel | No | Terminal-dependent |
| Kitty graphics | No | Terminal-dependent |

### E.3 WinPatina Implementation Priority

1. **Phase 1:** WinPatina core with 16-colour support
2. **Phase 2:** Colour quantisation (256/RGB → 16)
3. **Phase 3:** ncurses API compatibility via WinPatina
4. **Phase 4:** notcurses API compatibility via WinPatina (where feasible)
5. **Phase 5:** Performance optimisation (diff-based rendering)
