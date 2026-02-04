# WinPatina Implementation Plan

**Version:** 1.0
**Date:** 4 February 2026
**Target:** Windows 2000 through Windows 10 (pre-1511)

---

## Executive Summary

WinPatina is a VT-to-Win32 Console translation layer that enables modern terminal applications (notcurses, termina, and other VT-emitting libraries) to run on legacy Windows systems lacking native Virtual Terminal Processing support.

Based on comprehensive research of ncurses, PDCurses, notcurses, termina, Ink, Textual, and existing compatibility layers (ANSICON, ConPTY), this plan details a hybrid architecture that:

1. **Detects terminal capabilities** at startup
2. **Passes through VT sequences** on modern terminals (Windows Terminal, ConEmu)
3. **Translates VT to Win32 Console API** on legacy systems

---

## 1. Architecture

### 1.1 High-Level Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           winpatina.exe                                 │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                      Capability Detector                          │  │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐  │  │
│  │  │ WM_GETICON  │ │ ConEmuANSI  │ │ WT_SESSION  │ │SetConsoleMode│  │  │
│  │  │   check     │ │   env var   │ │   env var   │ │  0x0004 test │  │  │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                  │                                      │
│                 ┌────────────────┴────────────────┐                     │
│                 ▼                                 ▼                     │
│  ┌─────────────────────────┐      ┌─────────────────────────────────┐  │
│  │    VT Passthrough Mode  │      │      Win32 Translation Mode     │  │
│  │    (Modern Terminals)   │      │      (Legacy Console)           │  │
│  │                         │      │                                 │  │
│  │  • Forward VT to stdout │      │  ┌─────────────────────────┐   │  │
│  │  • Minimal processing   │      │  │      VT Parser          │   │  │
│  │  • Full colour support  │      │  │   (State Machine)       │   │  │
│  │                         │      │  └───────────┬─────────────┘   │  │
│  └─────────────────────────┘      │              │                 │  │
│                                   │              ▼                 │  │
│                                   │  ┌─────────────────────────┐   │  │
│                                   │  │    Screen Buffer        │   │  │
│                                   │  │   (CHAR_INFO array)     │   │  │
│                                   │  └───────────┬─────────────┘   │  │
│                                   │              │                 │  │
│                                   │              ▼                 │  │
│                                   │  ┌─────────────────────────┐   │  │
│                                   │  │   Win32 Renderer        │   │  │
│                                   │  │  (WriteConsoleOutputW)  │   │  │
│                                   │  └─────────────────────────┘   │  │
│                                   └─────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────┤
│                           Input Handler                                 │
│  ┌─────────────────────────┐      ┌─────────────────────────────────┐  │
│  │   ReadConsoleInputW()   │ ───▶ │   VT Input Sequence Generator   │  │
│  │   KEY_EVENT_RECORD      │      │   Mouse: \x1b[<btn;x;yM         │  │
│  │   MOUSE_EVENT_RECORD    │      │   Keys:  \x1b[A, \x1bOP, etc.   │  │
│  └─────────────────────────┘      └─────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────┤
│                         Process Manager                                 │
│  ┌─────────────────────────┐      ┌─────────────────────────────────┐  │
│  │   CreateProcess()       │      │   Pipe I/O                      │  │
│  │   Child stdin/stdout    │ ◀──▶ │   Async read/write              │  │
│  │   redirection           │      │   overlapped I/O                │  │
│  └─────────────────────────┘      └─────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Operating Modes

| Mode | Detection | Behaviour |
|------|-----------|-----------|
| **VT Passthrough** | `SetConsoleMode(0x0004)` succeeds, or Windows Terminal/ConEmu detected | Forward VT sequences directly to console; full colour/feature support |
| **Win32 Translation** | VT mode unavailable | Parse VT sequences, translate to Win32 Console API calls |
| **Hybrid** | VT mode available but incomplete | Use VT for supported features, Win32 for others |

### 1.3 Component Overview

| Component | File | Lines (est.) | Purpose |
|-----------|------|--------------|---------|
| Main/Process Manager | `winpatina_main.cpp` | ~300 | Entry point, process spawning, main loop |
| Capability Detector | `winpatina_detect.cpp` | ~200 | OS version, terminal type, feature detection |
| VT Parser | `winpatina_vt_parser.cpp` | ~1000 | State machine for CSI/OSC/DCS sequences |
| Screen Buffer | `winpatina_screen.cpp` | ~600 | CHAR_INFO management, diff-based updates |
| Colour Mapper | `winpatina_colour.cpp` | ~400 | RGB→16, 256→16 quantisation, palette |
| Win32 Renderer | `winpatina_render.cpp` | ~350 | WriteConsoleOutputW, cursor, attributes |
| Input Handler | `winpatina_input.cpp` | ~450 | ReadConsoleInput, key/mouse translation |
| VT Input Generator | `winpatina_vt_input.cpp` | ~300 | Generate VT sequences for input events |
| Query Responder | `winpatina_query.cpp` | ~200 | Terminal identification responses |
| UTF-8 Support | `winpatina_utf8.cpp` | ~150 | UTF-8 ↔ UTF-16 conversion |
| **Total** | | **~3,950** | |

---

## 2. Detailed Component Specifications

### 2.1 Capability Detector (`winpatina_detect.cpp`)

#### 2.1.1 Detection Hierarchy

```cpp
enum class WinPatinaMode {
    VT_PASSTHROUGH,      // Full VT support available
    WIN32_TRANSLATION,   // Legacy mode, translate everything
    HYBRID               // Partial VT support
};

enum class WindowsVersion {
    WIN_2000,            // NT 5.0 - No UTF-8 codepage
    WIN_XP,              // NT 5.1 - UTF-8 codepage available
    WIN_VISTA,           // NT 6.0 - COMMON_LVB_UNDERSCORE available
    WIN_7,               // NT 6.1
    WIN_8,               // NT 6.2/6.3
    WIN_10_LEGACY,       // 10.0 build < 10586 (pre-1511)
    WIN_10_VT,           // 10.0 build >= 10586 (VT available)
    WIN_11               // 10.0 build >= 22000
};

enum class TerminalType {
    LEGACY_CONSOLE,      // Classic conhost.exe
    WINDOWS_TERMINAL,    // Modern Windows Terminal
    CONEMU,              // ConEmu with ANSI support
    MINTTY,              // Cygwin/MSYS2 mintty
    OTHER_VT             // Other VT-capable terminal
};

struct WinPatinaCapabilities {
    WinPatinaMode mode;
    WindowsVersion os_version;
    TerminalType terminal;

    bool has_vt_processing;      // ENABLE_VIRTUAL_TERMINAL_PROCESSING works
    bool has_vt_input;           // ENABLE_VIRTUAL_TERMINAL_INPUT works
    bool has_utf8_codepage;      // CP_UTF8 (65001) available
    bool has_lvb_underscore;     // COMMON_LVB_UNDERSCORE available
    bool has_lvb_grid;           // COMMON_LVB_GRID_* available
    bool has_24bit_colour;       // Truecolour in VT mode
    bool has_256_colour;         // 256-colour palette
    bool has_alternate_buffer;   // CreateConsoleScreenBuffer works

    int max_colours;             // 16, 256, or 16777216
    COORD screen_size;           // Initial screen dimensions
};
```

#### 2.1.2 Detection Algorithm

```cpp
WinPatinaCapabilities detect_capabilities() {
    WinPatinaCapabilities caps = {};

    // 1. Detect Windows version using GetVersionExW (deprecated but works on Win2000+)
    OSVERSIONINFOW osvi = { sizeof(osvi) };
    GetVersionExW(&osvi);
    caps.os_version = classify_version(osvi);

    // 2. Check for modern terminal via environment
    if (getenv("WT_SESSION")) {
        caps.terminal = TerminalType::WINDOWS_TERMINAL;
    } else if (getenv("ConEmuANSI") && strcmp(getenv("ConEmuANSI"), "ON") == 0) {
        caps.terminal = TerminalType::CONEMU;
    } else if (getenv("TERM_PROGRAM") && strcmp(getenv("TERM_PROGRAM"), "mintty") == 0) {
        caps.terminal = TerminalType::MINTTY;
    }

    // 3. PDCurses technique: WM_GETICON returns 0 for Windows Terminal
    HWND console_window = GetConsoleWindow();
    if (console_window && !SendMessage(console_window, WM_GETICON, 0, 0)) {
        caps.terminal = TerminalType::WINDOWS_TERMINAL;
    }

    // 4. Test VT processing support
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
        if (SetConsoleMode(hOut, mode | 0x0004)) {  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            caps.has_vt_processing = true;
            SetConsoleMode(hOut, mode);  // Restore
        }
    }

    // 5. Test LVB attribute support (Vista+)
    if (SetConsoleMode(hOut, mode | 0x0010)) {  // ENABLE_LVB_GRID_WORLDWIDE
        caps.has_lvb_underscore = true;
        caps.has_lvb_grid = true;
        SetConsoleMode(hOut, mode);
    }

    // 6. Determine operating mode
    if (caps.has_vt_processing) {
        caps.mode = WinPatinaMode::VT_PASSTHROUGH;
        caps.max_colours = 16777216;  // Truecolour
    } else {
        caps.mode = WinPatinaMode::WIN32_TRANSLATION;
        caps.max_colours = 16;
    }

    // 7. Check UTF-8 codepage (XP+)
    caps.has_utf8_codepage = IsValidCodePage(65001);

    return caps;
}
```

### 2.2 VT Parser (`winpatina_vt_parser.cpp`)

#### 2.2.1 State Machine States

Based on the Paul Williams VT parser model, simplified for WinPatina's needs:

```cpp
enum class VTState {
    GROUND,              // Normal text processing
    ESCAPE,              // Received ESC (0x1B)
    ESCAPE_INTERMEDIATE, // ESC followed by intermediate byte
    CSI_ENTRY,           // Received ESC [
    CSI_PARAM,           // Accumulating CSI parameters
    CSI_INTERMEDIATE,    // CSI intermediate bytes
    CSI_IGNORE,          // Malformed CSI, consume until final
    OSC_STRING,          // Operating System Command string
    DCS_ENTRY,           // Device Control String entry
    DCS_PARAM,           // DCS parameters
    DCS_PASSTHROUGH,     // DCS data passthrough
    DCS_IGNORE,          // Malformed DCS
    SOS_PM_APC_STRING    // SOS/PM/APC string (ignored)
};
```

#### 2.2.2 Parser Structure

```cpp
#define WP_MAX_PARAMS 16
#define WP_MAX_INTERMEDIATES 4
#define WP_MAX_OSC_LEN 4096

struct VTParser {
    VTState state;

    // CSI parameter accumulation
    int params[WP_MAX_PARAMS];
    int param_count;
    int current_param;
    bool param_has_value;
    bool param_has_subparam;     // For colon-separated sub-parameters

    // Intermediate characters
    char intermediates[WP_MAX_INTERMEDIATES];
    int intermediate_count;

    // String accumulation (OSC, DCS, etc.)
    char string_buffer[WP_MAX_OSC_LEN];
    int string_len;

    // UTF-8 accumulation
    uint8_t utf8_buffer[4];
    int utf8_expected;
    int utf8_received;

    // Callbacks
    void* user_data;
    void (*on_print)(void* user_data, const wchar_t* text, int len);
    void (*on_execute)(void* user_data, uint8_t byte);
    void (*on_csi_dispatch)(void* user_data, const VTParser* parser, char final_byte);
    void (*on_esc_dispatch)(void* user_data, const VTParser* parser, char final_byte);
    void (*on_osc_dispatch)(void* user_data, const VTParser* parser);
    void (*on_dcs_dispatch)(void* user_data, const VTParser* parser, char final_byte);
};

// Initialisation
void vt_parser_init(VTParser* parser, void* user_data);

// Feed data to parser
void vt_parser_feed(VTParser* parser, const char* data, size_t len);

// Reset parser state
void vt_parser_reset(VTParser* parser);

// Utility: get parameter with default
int vt_get_param(const VTParser* parser, int index, int default_value);

// Utility: check for intermediate character
bool vt_has_intermediate(const VTParser* parser, char ch);
```

#### 2.2.3 Supported Sequences

**Tier 1: Essential (Must implement)**

| Sequence | Name | Handler |
|----------|------|---------|
| `ESC [ H` | CUP - Cursor Position | `handle_cup()` |
| `ESC [ A/B/C/D` | CUU/CUD/CUF/CUB - Cursor Movement | `handle_cursor_move()` |
| `ESC [ G` | CHA - Cursor Horizontal Absolute | `handle_cha()` |
| `ESC [ d` | VPA - Vertical Position Absolute | `handle_vpa()` |
| `ESC [ J` | ED - Erase in Display | `handle_ed()` |
| `ESC [ K` | EL - Erase in Line | `handle_el()` |
| `ESC [ m` | SGR - Select Graphic Rendition | `handle_sgr()` |
| `ESC [ ? 25 h/l` | DECTCEM - Cursor Visibility | `handle_cursor_visibility()` |
| `ESC [ ? 1049 h/l` | Alternate Screen Buffer | `handle_alternate_screen()` |
| `ESC 7` / `ESC 8` | DECSC/DECRC - Save/Restore Cursor | `handle_save_restore_cursor()` |

**Tier 2: Important (Should implement)**

| Sequence | Name | Handler |
|----------|------|---------|
| `ESC [ 38;5;N m` | 256-colour Foreground | `handle_sgr_256()` |
| `ESC [ 48;5;N m` | 256-colour Background | `handle_sgr_256()` |
| `ESC [ 38;2;R;G;B m` | RGB Foreground | `handle_sgr_rgb()` |
| `ESC [ 48;2;R;G;B m` | RGB Background | `handle_sgr_rgb()` |
| `ESC [ S` / `ESC [ T` | SU/SD - Scroll Up/Down | `handle_scroll()` |
| `ESC [ L` / `ESC [ M` | IL/DL - Insert/Delete Lines | `handle_line_edit()` |
| `ESC [ @` / `ESC [ P` | ICH/DCH - Insert/Delete Characters | `handle_char_edit()` |
| `ESC [ ? 1000 h/l` | Mouse Button Tracking | `handle_mouse_mode()` |
| `ESC [ ? 1002 h/l` | Mouse Button Event Tracking | `handle_mouse_mode()` |
| `ESC [ ? 1003 h/l` | Mouse Any Event Tracking | `handle_mouse_mode()` |
| `ESC [ ? 1006 h/l` | SGR Mouse Mode | `handle_mouse_mode()` |
| `ESC [ ? 2026 h/l` | Synchronised Output | `handle_sync_output()` |

**Tier 3: Nice-to-have (Can implement later)**

| Sequence | Name | Handler |
|----------|------|---------|
| `ESC [ c` | DA1 - Primary Device Attributes | `handle_device_attributes()` |
| `ESC [ > c` | DA2 - Secondary Device Attributes | `handle_device_attributes()` |
| `ESC [ 6 n` | DSR - Cursor Position Report | `handle_device_status()` |
| `ESC [ r` | DECSTBM - Set Scrolling Region | `handle_scroll_region()` |
| `ESC ] 0 ; ... ST` | OSC - Set Window Title | `handle_osc()` |
| `ESC ] 52 ; ... ST` | OSC 52 - Clipboard | `handle_osc_clipboard()` |

### 2.3 Screen Buffer (`winpatina_screen.cpp`)

#### 2.3.1 Screen Buffer Structure

```cpp
struct ScreenCell {
    wchar_t character;       // Unicode character (or high surrogate)
    wchar_t character_low;   // Low surrogate for characters outside BMP
    WORD attributes;         // Win32 console attributes
    bool dirty;              // Changed since last render
};

struct ScreenBuffer {
    ScreenCell* cells;       // Row-major array
    int width;
    int height;

    // Cursor state
    COORD cursor_position;
    bool cursor_visible;
    int cursor_style;        // 0=default, 1=block, 2=underline, 3=bar
    COORD saved_cursor;

    // Attribute state
    WORD current_attrs;
    WORD default_attrs;

    // Scroll region
    int scroll_top;
    int scroll_bottom;

    // Dirty tracking
    int dirty_top;           // First dirty row
    int dirty_bottom;        // Last dirty row + 1
    bool full_repaint;       // Force full repaint

    // Alternate buffer
    ScreenBuffer* alternate;
    bool using_alternate;
};

// Create/destroy
ScreenBuffer* screen_create(int width, int height, WORD default_attrs);
void screen_destroy(ScreenBuffer* screen);

// Resize
void screen_resize(ScreenBuffer* screen, int new_width, int new_height);

// Cell access
ScreenCell* screen_cell_at(ScreenBuffer* screen, int x, int y);
void screen_set_char(ScreenBuffer* screen, int x, int y, wchar_t ch);
void screen_set_attr(ScreenBuffer* screen, int x, int y, WORD attr);

// Region operations
void screen_fill(ScreenBuffer* screen, int x, int y, int w, int h, wchar_t ch, WORD attr);
void screen_scroll(ScreenBuffer* screen, int lines);  // Positive = up
void screen_insert_lines(ScreenBuffer* screen, int y, int count);
void screen_delete_lines(ScreenBuffer* screen, int y, int count);

// Dirty tracking
void screen_mark_dirty(ScreenBuffer* screen, int y);
void screen_mark_dirty_range(ScreenBuffer* screen, int y1, int y2);
void screen_mark_clean(ScreenBuffer* screen);

// Alternate buffer
void screen_enter_alternate(ScreenBuffer* screen);
void screen_leave_alternate(ScreenBuffer* screen);
```

#### 2.3.2 Diff-Based Rendering

Following PDCurses' approach, group cells by attribute before writing:

```cpp
struct RenderPacket {
    int x, y;
    int length;
    WORD attributes;
    wchar_t* text;
};

// Generate packets from dirty regions
std::vector<RenderPacket> screen_generate_packets(ScreenBuffer* screen) {
    std::vector<RenderPacket> packets;

    for (int y = screen->dirty_top; y < screen->dirty_bottom; y++) {
        int x = 0;
        while (x < screen->width) {
            ScreenCell* cell = screen_cell_at(screen, x, y);
            if (!cell->dirty) {
                x++;
                continue;
            }

            // Start a new packet
            RenderPacket packet;
            packet.x = x;
            packet.y = y;
            packet.attributes = cell->attributes;

            // Accumulate cells with same attributes
            int start_x = x;
            while (x < screen->width) {
                cell = screen_cell_at(screen, x, y);
                if (!cell->dirty || cell->attributes != packet.attributes) {
                    break;
                }
                x++;
            }

            packet.length = x - start_x;
            // Copy text...
            packets.push_back(packet);
        }
    }

    return packets;
}
```

### 2.4 Colour Mapper (`winpatina_colour.cpp`)

#### 2.4.1 Colour Translation Tables

```cpp
// ANSI colour index to Win32 attribute (note: BGR order in Win32)
// ANSI order:  Black, Red, Green, Yellow, Blue, Magenta, Cyan, White
// Win32 order: Black, Blue, Green, Cyan, Red, Magenta, Yellow, White

static const WORD ansi_to_win32_fg[16] = {
    0,                                              // 0: Black
    FOREGROUND_RED,                                 // 1: Red
    FOREGROUND_GREEN,                               // 2: Green
    FOREGROUND_RED | FOREGROUND_GREEN,              // 3: Yellow
    FOREGROUND_BLUE,                                // 4: Blue
    FOREGROUND_RED | FOREGROUND_BLUE,               // 5: Magenta
    FOREGROUND_GREEN | FOREGROUND_BLUE,             // 6: Cyan
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,  // 7: White
    FOREGROUND_INTENSITY,                           // 8: Bright Black
    FOREGROUND_RED | FOREGROUND_INTENSITY,          // 9: Bright Red
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,        // 10: Bright Green
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,  // 11: Bright Yellow
    FOREGROUND_BLUE | FOREGROUND_INTENSITY,         // 12: Bright Blue
    FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,   // 13: Bright Magenta
    FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY, // 14: Bright Cyan
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY  // 15: Bright White
};

// Background: shift left by 4 bits
static const WORD ansi_to_win32_bg[16] = { /* same values << 4 */ };
```

#### 2.4.2 256-Colour to 16-Colour Mapping

The 256-colour palette has three regions:
- 0-15: Standard ANSI colours (direct map)
- 16-231: 6x6x6 colour cube
- 232-255: Greyscale ramp

```cpp
struct RGB { uint8_t r, g, b; };

// Standard 16-colour palette (approximate sRGB values)
static const RGB palette_16[16] = {
    {0, 0, 0},       // Black
    {128, 0, 0},     // Red
    {0, 128, 0},     // Green
    {128, 128, 0},   // Yellow
    {0, 0, 128},     // Blue
    {128, 0, 128},   // Magenta
    {0, 128, 128},   // Cyan
    {192, 192, 192}, // White
    {128, 128, 128}, // Bright Black
    {255, 0, 0},     // Bright Red
    {0, 255, 0},     // Bright Green
    {255, 255, 0},   // Bright Yellow
    {0, 0, 255},     // Bright Blue
    {255, 0, 255},   // Bright Magenta
    {0, 255, 255},   // Bright Cyan
    {255, 255, 255}  // Bright White
};

// Convert 256-colour index to RGB
RGB colour_256_to_rgb(int index) {
    if (index < 16) {
        return palette_16[index];
    } else if (index < 232) {
        // 6x6x6 colour cube
        index -= 16;
        int r = (index / 36) * 51;
        int g = ((index / 6) % 6) * 51;
        int b = (index % 6) * 51;
        return {(uint8_t)r, (uint8_t)g, (uint8_t)b};
    } else {
        // Greyscale: 24 shades from 8 to 238
        int grey = 8 + (index - 232) * 10;
        return {(uint8_t)grey, (uint8_t)grey, (uint8_t)grey};
    }
}

// Euclidean distance in RGB space (simple but fast)
int colour_distance_rgb(RGB a, RGB b) {
    int dr = a.r - b.r;
    int dg = a.g - b.g;
    int db = a.b - b.b;
    return dr*dr + dg*dg + db*db;
}

// Find nearest 16-colour match
int colour_rgb_to_16(RGB colour) {
    int best_index = 0;
    int best_distance = INT_MAX;

    for (int i = 0; i < 16; i++) {
        int dist = colour_distance_rgb(colour, palette_16[i]);
        if (dist < best_distance) {
            best_distance = dist;
            best_index = i;
        }
    }

    return best_index;
}

// Convert 256-colour to 16-colour
int colour_256_to_16(int index) {
    if (index < 16) {
        return index;  // Direct mapping for standard colours
    }
    return colour_rgb_to_16(colour_256_to_rgb(index));
}

// Convert RGB to 16-colour
int colour_rgb_to_16_direct(int r, int g, int b) {
    return colour_rgb_to_16({(uint8_t)r, (uint8_t)g, (uint8_t)b});
}
```

#### 2.4.3 Optional: Perceptual Colour Distance (CIE Lab)

For better colour matching, convert to CIE Lab colour space:

```cpp
// sRGB to linear RGB
double srgb_to_linear(double c) {
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// Linear RGB to XYZ (D65 illuminant)
void rgb_to_xyz(RGB rgb, double* x, double* y, double* z) {
    double r = srgb_to_linear(rgb.r / 255.0);
    double g = srgb_to_linear(rgb.g / 255.0);
    double b = srgb_to_linear(rgb.b / 255.0);

    *x = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
    *y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
    *z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
}

// XYZ to CIE Lab
void xyz_to_lab(double x, double y, double z, double* L, double* a, double* b) {
    // D65 reference white
    x /= 0.95047;
    y /= 1.00000;
    z /= 1.08883;

    auto f = [](double t) {
        return (t > 0.008856) ? pow(t, 1.0/3.0) : (7.787 * t + 16.0/116.0);
    };

    *L = 116.0 * f(y) - 16.0;
    *a = 500.0 * (f(x) - f(y));
    *b = 200.0 * (f(y) - f(z));
}

// CIE76 colour difference
double colour_distance_lab(RGB c1, RGB c2) {
    double x1, y1, z1, x2, y2, z2;
    double L1, a1, b1, L2, a2, b2;

    rgb_to_xyz(c1, &x1, &y1, &z1);
    rgb_to_xyz(c2, &x2, &y2, &z2);
    xyz_to_lab(x1, y1, z1, &L1, &a1, &b1);
    xyz_to_lab(x2, y2, z2, &L2, &a2, &b2);

    double dL = L1 - L2;
    double da = a1 - a2;
    double db = b1 - b2;

    return sqrt(dL*dL + da*da + db*db);
}
```

### 2.5 Win32 Renderer (`winpatina_render.cpp`)

#### 2.5.1 Rendering Functions

```cpp
struct Win32Renderer {
    HANDLE hConsole;
    HANDLE hMainBuffer;
    HANDLE hAltBuffer;
    bool using_alt_buffer;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    CONSOLE_CURSOR_INFO cci;

    // Cached state to avoid redundant API calls
    COORD last_cursor_pos;
    WORD last_attributes;
    bool cursor_hidden;
};

// Initialise renderer
void renderer_init(Win32Renderer* r);

// Render packets to console
void renderer_draw_packets(Win32Renderer* r, const std::vector<RenderPacket>& packets) {
    for (const auto& packet : packets) {
        // Build CHAR_INFO array
        std::vector<CHAR_INFO> buffer(packet.length);
        for (int i = 0; i < packet.length; i++) {
            buffer[i].Char.UnicodeChar = packet.text[i];
            buffer[i].Attributes = packet.attributes;
        }

        // Set up coordinates
        COORD bufSize = { (SHORT)packet.length, 1 };
        COORD bufPos = { 0, 0 };
        SMALL_RECT writeRegion = {
            (SHORT)packet.x,
            (SHORT)packet.y,
            (SHORT)(packet.x + packet.length - 1),
            (SHORT)packet.y
        };

        WriteConsoleOutputW(r->hConsole, buffer.data(), bufSize, bufPos, &writeRegion);
    }
}

// Bottom-right cell handling (PDCurses technique)
void renderer_draw_bottom_right(Win32Renderer* r, wchar_t ch, WORD attr) {
    // Position cursor at second-to-last cell
    COORD pos = { (SHORT)(r->csbi.dwSize.X - 2), (SHORT)(r->csbi.dwSize.Y - 1) };
    SetConsoleCursorPosition(r->hConsole, pos);

    // Write two characters, then move cursor back
    CHAR_INFO ci[2];
    ci[0].Char.UnicodeChar = ch;
    ci[0].Attributes = attr;
    ci[1].Char.UnicodeChar = L' ';
    ci[1].Attributes = attr;

    COORD bufSize = { 2, 1 };
    COORD bufPos = { 0, 0 };
    SMALL_RECT region = { pos.X, pos.Y, (SHORT)(pos.X + 1), pos.Y };

    WriteConsoleOutputW(r->hConsole, ci, bufSize, bufPos, &region);
}

// Cursor control
void renderer_set_cursor_pos(Win32Renderer* r, int x, int y) {
    COORD pos = { (SHORT)x, (SHORT)y };
    if (pos.X != r->last_cursor_pos.X || pos.Y != r->last_cursor_pos.Y) {
        SetConsoleCursorPosition(r->hConsole, pos);
        r->last_cursor_pos = pos;
    }
}

void renderer_set_cursor_visible(Win32Renderer* r, bool visible) {
    if (visible != !r->cursor_hidden) {
        CONSOLE_CURSOR_INFO cci;
        GetConsoleCursorInfo(r->hConsole, &cci);
        cci.bVisible = visible ? TRUE : FALSE;
        SetConsoleCursorInfo(r->hConsole, &cci);
        r->cursor_hidden = !visible;
    }
}

// Alternate buffer
void renderer_enter_alt_buffer(Win32Renderer* r) {
    if (!r->using_alt_buffer) {
        r->hMainBuffer = r->hConsole;
        r->hAltBuffer = CreateConsoleScreenBuffer(
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CONSOLE_TEXTMODE_BUFFER,
            NULL
        );
        SetConsoleActiveScreenBuffer(r->hAltBuffer);
        r->hConsole = r->hAltBuffer;
        r->using_alt_buffer = true;

        // Clear alternate buffer
        GetConsoleScreenBufferInfo(r->hConsole, &r->csbi);
        DWORD written;
        COORD origin = { 0, 0 };
        DWORD size = r->csbi.dwSize.X * r->csbi.dwSize.Y;
        FillConsoleOutputCharacterW(r->hConsole, L' ', size, origin, &written);
        FillConsoleOutputAttribute(r->hConsole, r->csbi.wAttributes, size, origin, &written);
    }
}

void renderer_leave_alt_buffer(Win32Renderer* r) {
    if (r->using_alt_buffer) {
        SetConsoleActiveScreenBuffer(r->hMainBuffer);
        CloseHandle(r->hAltBuffer);
        r->hConsole = r->hMainBuffer;
        r->hAltBuffer = NULL;
        r->using_alt_buffer = false;
    }
}

// Scrolling
void renderer_scroll(Win32Renderer* r, int top, int bottom, int lines) {
    SMALL_RECT scroll_rect = {
        0,
        (SHORT)top,
        (SHORT)(r->csbi.dwSize.X - 1),
        (SHORT)bottom
    };
    COORD dest = { 0, (SHORT)(top - lines) };
    CHAR_INFO fill = { L' ', r->csbi.wAttributes };

    ScrollConsoleScreenBufferW(r->hConsole, &scroll_rect, &scroll_rect, dest, &fill);
}
```

### 2.6 Input Handler (`winpatina_input.cpp`)

#### 2.6.1 Input Processing

```cpp
struct InputState {
    HANDLE hInput;

    // Mouse state
    bool mouse_enabled;
    int mouse_mode;          // 1000, 1002, 1003, 1006
    bool mouse_sgr_mode;     // Use SGR format
    DWORD last_button_state;
    COORD last_mouse_pos;
    DWORD last_click_time;   // For double-click detection
    int click_count;

    // Keyboard state
    bool app_cursor_keys;    // DECCKM mode
    bool app_keypad_mode;    // DECKPAM/DECKPNM
};

// Process a single input record
void input_process_record(InputState* state, INPUT_RECORD* record,
                          std::string& output_buffer) {
    switch (record->EventType) {
        case KEY_EVENT:
            input_process_key(state, &record->Event.KeyEvent, output_buffer);
            break;
        case MOUSE_EVENT:
            if (state->mouse_enabled) {
                input_process_mouse(state, &record->Event.MouseEvent, output_buffer);
            }
            break;
        case WINDOW_BUFFER_SIZE_EVENT:
            // Generate SIGWINCH-equivalent (could use custom escape sequence)
            break;
    }
}

// Key event processing
void input_process_key(InputState* state, KEY_EVENT_RECORD* key,
                       std::string& output) {
    if (!key->bKeyDown) {
        return;  // Ignore key-up events (mostly)
    }

    WORD vk = key->wVirtualKeyCode;
    wchar_t ch = key->uChar.UnicodeChar;
    DWORD ctrl = key->dwControlKeyState;

    bool shift = ctrl & SHIFT_PRESSED;
    bool alt = ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);
    bool ctrl_key = ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);

    // Handle special keys
    switch (vk) {
        case VK_UP:
            output += state->app_cursor_keys ? "\x1bOA" : "\x1b[A";
            break;
        case VK_DOWN:
            output += state->app_cursor_keys ? "\x1bOB" : "\x1b[B";
            break;
        case VK_RIGHT:
            output += state->app_cursor_keys ? "\x1bOC" : "\x1b[C";
            break;
        case VK_LEFT:
            output += state->app_cursor_keys ? "\x1bOD" : "\x1b[D";
            break;
        case VK_HOME:
            output += "\x1b[H";
            break;
        case VK_END:
            output += "\x1b[F";
            break;
        case VK_INSERT:
            output += "\x1b[2~";
            break;
        case VK_DELETE:
            output += "\x1b[3~";
            break;
        case VK_PRIOR:  // Page Up
            output += "\x1b[5~";
            break;
        case VK_NEXT:   // Page Down
            output += "\x1b[6~";
            break;
        case VK_F1: case VK_F2: case VK_F3: case VK_F4:
            // F1-F4 use SS3 format
            output += "\x1bO";
            output += (char)('P' + (vk - VK_F1));
            break;
        case VK_F5: case VK_F6: case VK_F7: case VK_F8:
        case VK_F9: case VK_F10: case VK_F11: case VK_F12:
            // F5-F12 use CSI format
            {
                static const int f_codes[] = { 15, 17, 18, 19, 20, 21, 23, 24 };
                char buf[16];
                snprintf(buf, sizeof(buf), "\x1b[%d~", f_codes[vk - VK_F5]);
                output += buf;
            }
            break;
        case VK_ESCAPE:
            output += "\x1b";
            break;
        case VK_RETURN:
            output += "\r";
            break;
        case VK_TAB:
            output += shift ? "\x1b[Z" : "\t";
            break;
        case VK_BACK:
            output += "\x7f";
            break;
        default:
            // Regular character
            if (ch != 0) {
                if (alt && ch < 128) {
                    // Alt+key sends ESC + key
                    output += "\x1b";
                }
                // Convert UTF-16 to UTF-8
                char utf8[4];
                int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, sizeof(utf8), NULL, NULL);
                output.append(utf8, len);
            }
            break;
    }
}

// Mouse event processing (SGR 1006 format)
void input_process_mouse(InputState* state, MOUSE_EVENT_RECORD* mouse,
                         std::string& output) {
    int x = mouse->dwMousePosition.X + 1;  // 1-indexed
    int y = mouse->dwMousePosition.Y + 1;
    DWORD buttons = mouse->dwButtonState;
    DWORD flags = mouse->dwEventFlags;

    int button = 0;
    char action = 'M';  // Press/drag

    if (flags == MOUSE_WHEELED) {
        // Wheel event
        button = (mouse->dwButtonState & 0xFF000000) ? 65 : 64;  // Down : Up
        action = 'M';
    } else if (flags == MOUSE_MOVED) {
        if (state->mouse_mode < 1002) {
            return;  // Motion not requested
        }
        button = 32;  // Motion indicator
        if (buttons & FROM_LEFT_1ST_BUTTON_PRESSED) button += 0;
        else if (buttons & RIGHTMOST_BUTTON_PRESSED) button += 2;
        else if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED) button += 1;
        else if (state->mouse_mode < 1003) {
            return;  // Motion without button, not tracking all events
        } else {
            button = 35;  // Motion with no button
        }
    } else {
        // Button press/release
        if (buttons & FROM_LEFT_1ST_BUTTON_PRESSED) button = 0;
        else if (buttons & RIGHTMOST_BUTTON_PRESSED) button = 2;
        else if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED) button = 1;

        // Detect release
        DWORD released = state->last_button_state & ~buttons;
        if (released) {
            action = 'm';  // Release
            if (released & FROM_LEFT_1ST_BUTTON_PRESSED) button = 0;
            else if (released & RIGHTMOST_BUTTON_PRESSED) button = 2;
            else if (released & FROM_LEFT_2ND_BUTTON_PRESSED) button = 1;
        }
    }

    state->last_button_state = buttons;
    state->last_mouse_pos = mouse->dwMousePosition;

    // Generate SGR mouse sequence
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[<%d;%d;%d%c", button, x, y, action);
    output += buf;
}
```

### 2.7 UTF-8 Support (`winpatina_utf8.cpp`)

```cpp
// Convert UTF-8 string to UTF-16 (Windows native)
std::wstring utf8_to_utf16(const char* utf8, int len) {
    if (len == 0) return L"";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, len, NULL, 0);
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, len, &result[0], wlen);
    return result;
}

// Convert UTF-16 string to UTF-8
std::string utf16_to_utf8(const wchar_t* utf16, int len) {
    if (len == 0) return "";

    int ulen = WideCharToMultiByte(CP_UTF8, 0, utf16, len, NULL, 0, NULL, NULL);
    std::string result(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16, len, &result[0], ulen, NULL, NULL);
    return result;
}

// Check if byte is UTF-8 continuation byte
inline bool utf8_is_continuation(uint8_t byte) {
    return (byte & 0xC0) == 0x80;
}

// Get expected length of UTF-8 sequence from lead byte
int utf8_sequence_length(uint8_t lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;  // Invalid
}
```

---

## 3. Public API (`winpatina.h`)

```cpp
#ifndef WINPATINA_H
#define WINPATINA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Version information
#define WINPATINA_VERSION_MAJOR 1
#define WINPATINA_VERSION_MINOR 0
#define WINPATINA_VERSION_PATCH 0

// Opaque handle
typedef struct WinPatina WinPatina;

// Capability flags
typedef enum {
    WP_CAP_VT_PROCESSING   = 0x0001,
    WP_CAP_VT_INPUT        = 0x0002,
    WP_CAP_UTF8_CODEPAGE   = 0x0004,
    WP_CAP_UNDERSCORE      = 0x0008,
    WP_CAP_GRID_LINES      = 0x0010,
    WP_CAP_256_COLOURS     = 0x0020,
    WP_CAP_TRUECOLOUR      = 0x0040,
    WP_CAP_MOUSE           = 0x0080,
    WP_CAP_ALT_BUFFER      = 0x0100
} WinPatinaCapability;

// Configuration options
typedef struct {
    bool force_translation;     // Force Win32 mode even if VT available
    bool enable_mouse;          // Enable mouse input translation
    bool enable_utf8;           // Enable UTF-8 (if available)
    int colour_mode;            // 0=auto, 16, 256
} WinPatinaConfig;

// Initialise WinPatina
// Returns NULL on failure
WinPatina* wp_init(const WinPatinaConfig* config);

// Clean up
void wp_destroy(WinPatina* wp);

// Get detected capabilities
uint32_t wp_get_capabilities(WinPatina* wp);

// Get screen dimensions
void wp_get_screen_size(WinPatina* wp, int* width, int* height);

// Launch child process
// Returns 0 on success, -1 on failure
int wp_spawn(WinPatina* wp, const char* command, char* const argv[]);

// Main loop iteration (non-blocking)
// Returns: 1=child exited, 0=continue, -1=error
int wp_poll(WinPatina* wp, int timeout_ms);

// Get child exit code (valid after wp_poll returns 1)
int wp_get_exit_code(WinPatina* wp);

// Direct write to child stdin (for advanced use)
int wp_write_child(WinPatina* wp, const char* data, int len);

// Force screen refresh
void wp_refresh(WinPatina* wp);

// Set window title
void wp_set_title(WinPatina* wp, const char* title);

#ifdef __cplusplus
}
#endif

#endif // WINPATINA_H
```

---

## 4. Build System

### 4.1 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(winpatina VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 2000 targeting
if(MINGW)
    add_compile_definitions(
        _WIN32_WINNT=0x0500
        WINVER=0x0500
        NTDDI_VERSION=0x05000000
    )
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--subsystem,console:5.0")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# Source files
set(WINPATINA_SOURCES
    src/winpatina_main.cpp
    src/winpatina_detect.cpp
    src/winpatina_vt_parser.cpp
    src/winpatina_screen.cpp
    src/winpatina_colour.cpp
    src/winpatina_render.cpp
    src/winpatina_input.cpp
    src/winpatina_vt_input.cpp
    src/winpatina_query.cpp
    src/winpatina_utf8.cpp
)

# Library
add_library(winpatina_lib STATIC ${WINPATINA_SOURCES})
target_include_directories(winpatina_lib PUBLIC include)

# Executable
add_executable(winpatina src/main.cpp)
target_link_libraries(winpatina PRIVATE winpatina_lib)

# Tests
enable_testing()
add_subdirectory(tests)
```

### 4.2 Directory Structure

```
winpatina/
├── CMakeLists.txt
├── README.md
├── IMPLEMENTATION_PLAN.md
├── include/
│   └── winpatina.h
├── src/
│   ├── main.cpp
│   ├── winpatina_main.cpp
│   ├── winpatina_detect.cpp
│   ├── winpatina_vt_parser.cpp
│   ├── winpatina_screen.cpp
│   ├── winpatina_colour.cpp
│   ├── winpatina_render.cpp
│   ├── winpatina_input.cpp
│   ├── winpatina_vt_input.cpp
│   ├── winpatina_query.cpp
│   ├── winpatina_utf8.cpp
│   └── internal.h
├── tests/
│   ├── CMakeLists.txt
│   ├── test_vt_parser.cpp
│   ├── test_colour.cpp
│   ├── test_screen.cpp
│   └── test_input.cpp
└── examples/
    ├── simple.cpp
    └── notcurses_demo.cpp
```

---

## 5. Implementation Phases

### Phase 1: Foundation (MVP)
**Goal:** Run a simple VT application on Windows 7

| Task | Est. Time | Dependencies |
|------|-----------|--------------|
| Project setup, CMake, headers | - | None |
| Capability detector | - | None |
| VT parser (CSI only, Tier 1 sequences) | - | None |
| Screen buffer (basic) | - | None |
| Win32 renderer (WriteConsoleOutputW) | - | Screen buffer |
| 16-colour support | - | None |
| Cursor control | - | Renderer |
| Main loop with pipe I/O | - | All above |
| **Milestone: echo test works** | | |

### Phase 2: Essential Features
**Goal:** Run notcurses applications with basic functionality

| Task | Est. Time | Dependencies |
|------|-----------|--------------|
| Alternate screen buffer | - | Renderer |
| 256-colour quantisation | - | Colour mapper |
| RGB colour quantisation | - | Colour mapper |
| Keyboard input translation | - | Input handler |
| Mouse input (buttons only) | - | Input handler |
| Scroll operations | - | Screen buffer |
| **Milestone: notcurses-demo runs** | | |

### Phase 3: Full Compatibility
**Goal:** Full notcurses/ncurses compatibility

| Task | Est. Time | Dependencies |
|------|-----------|--------------|
| Mouse motion tracking | - | Input handler |
| Mouse wheel support | - | Input handler |
| SGR extended attributes (underline) | - | Renderer |
| Synchronised output buffering | - | Renderer |
| Terminal query responses | - | Query responder |
| Insert/delete line operations | - | Screen buffer |
| Insert/delete character operations | - | Screen buffer |
| **Milestone: full test suite passes** | | |

### Phase 4: Polish
**Goal:** Production-ready release

| Task | Est. Time | Dependencies |
|------|-----------|--------------|
| Performance optimisation | - | All |
| Perceptual colour matching (CIE Lab) | - | Colour mapper |
| Wide character handling | - | UTF-8, screen |
| Error handling and recovery | - | All |
| Documentation | - | None |
| Example applications | - | All |
| **Milestone: 1.0 release** | | |

---

## 6. Testing Strategy

### 6.1 Unit Tests

| Component | Test Cases |
|-----------|------------|
| VT Parser | CSI parameter parsing, SGR attribute accumulation, state transitions, malformed sequences |
| Colour Mapper | 256→16 mapping accuracy, RGB→16 mapping, edge cases (black, white, greys) |
| Screen Buffer | Cell access, dirty tracking, scroll operations, resize handling |
| Input Handler | Key translation, modifier keys, mouse button states, wheel events |

### 6.2 Integration Tests

| Test | Description |
|------|-------------|
| Echo test | Simple program that echoes input, verify rendering |
| Colour test | Display all 256 colours, verify degradation |
| Mouse test | Track mouse movement and clicks |
| Resize test | Handle terminal resize events |
| notcurses-info | Run notcurses capability detection |
| notcurses-demo | Run full demo application |

### 6.3 Platform Testing Matrix

| OS | Console | Expected Mode |
|----|---------|---------------|
| Windows 2000 | Legacy | Win32 Translation |
| Windows XP | Legacy | Win32 Translation |
| Windows 7 | Legacy | Win32 Translation |
| Windows 8.1 | Legacy | Win32 Translation |
| Windows 10 (1507) | Legacy | Win32 Translation |
| Windows 10 (1511+) | VT-capable | VT Passthrough |
| Windows Terminal | Modern | VT Passthrough |
| ConEmu (ANSI=ON) | Hybrid | VT Passthrough |

---

## 7. Known Limitations

### 7.1 Features Not Supported

| Feature | Reason |
|---------|--------|
| Italic text | No Win32 Console equivalent |
| Strikethrough | No Win32 Console equivalent |
| Sixel graphics | Not feasible on text console |
| Kitty graphics | Not feasible on text console |
| iTerm2 inline images | Not feasible on text console |
| 24-bit colour (legacy) | Hardware limitation (16 colours only) |
| Blinking text | Possible but rarely useful |

### 7.2 Degraded Features

| Feature | Modern | Legacy |
|---------|--------|--------|
| Colours | 16.7M | 16 |
| Underline | Full | Vista+ only |
| Mouse precision | Pixel | Cell |
| Unicode | Full | Font-dependent |
| Performance | Native | ~95% native |

---

## 8. References

### 8.1 Specifications
- [ECMA-48: Control Functions for Coded Character Sets](https://www.ecma-international.org/publications-and-standards/standards/ecma-48/)
- [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
- [VT100.net Parser](https://vt100.net/emu/dec_ansi_parser)

### 8.2 Win32 Console API
- [Console Functions](https://learn.microsoft.com/en-us/windows/console/console-functions)
- [Console Virtual Terminal Sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences)

### 8.3 Reference Implementations
- [ncurses win32con driver](https://invisible-island.net/ncurses/)
- [PDCurses wincon backend](https://pdcurses.org/)
- [ANSICON](https://github.com/adoxa/ansicon)
- [Windows Terminal](https://github.com/microsoft/terminal)

---

## Appendix A: Win32 Console Attribute Bits

```
Bit 0 (0x0001): FOREGROUND_BLUE
Bit 1 (0x0002): FOREGROUND_GREEN
Bit 2 (0x0004): FOREGROUND_RED
Bit 3 (0x0008): FOREGROUND_INTENSITY

Bit 4 (0x0010): BACKGROUND_BLUE
Bit 5 (0x0020): BACKGROUND_GREEN
Bit 6 (0x0040): BACKGROUND_RED
Bit 7 (0x0080): BACKGROUND_INTENSITY

Bit 8  (0x0100): COMMON_LVB_LEADING_BYTE
Bit 9  (0x0200): COMMON_LVB_TRAILING_BYTE
Bit 10 (0x0400): COMMON_LVB_GRID_HORIZONTAL
Bit 11 (0x0800): COMMON_LVB_GRID_LVERTICAL
Bit 12 (0x1000): COMMON_LVB_GRID_RVERTICAL
Bit 13 (0x2000): COMMON_LVB_REVERSE_VIDEO
Bit 14 (0x4000): COMMON_LVB_UNDERSCORE
Bit 15 (0x8000): (Sbcs/Dbcs padding)
```

## Appendix B: ANSI to Win32 Colour Mapping (Corrected)

```
ANSI Index | ANSI Name      | Win32 Value | Win32 Bits (BGR)
-----------|----------------|-------------|------------------
0          | Black          | 0x00        | 000
1          | Red            | 0x04        | 100 (RED)
2          | Green          | 0x02        | 010 (GREEN)
3          | Yellow         | 0x06        | 110 (RED|GREEN)
4          | Blue           | 0x01        | 001 (BLUE)
5          | Magenta        | 0x05        | 101 (RED|BLUE)
6          | Cyan           | 0x03        | 011 (GREEN|BLUE)
7          | White          | 0x07        | 111 (RED|GREEN|BLUE)
8          | Bright Black   | 0x08        | INTENSITY
9          | Bright Red     | 0x0C        | INTENSITY|RED
10         | Bright Green   | 0x0A        | INTENSITY|GREEN
11         | Bright Yellow  | 0x0E        | INTENSITY|RED|GREEN
12         | Bright Blue    | 0x09        | INTENSITY|BLUE
13         | Bright Magenta | 0x0D        | INTENSITY|RED|BLUE
14         | Bright Cyan    | 0x0B        | INTENSITY|GREEN|BLUE
15         | Bright White   | 0x0F        | INTENSITY|RED|GREEN|BLUE
```

Note: The ANSI standard uses RGB order, but Win32 Console uses BGR order. The mapping table above accounts for this difference.
