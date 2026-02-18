/**
 * @file winpatina_render.cpp
 * @brief Win32 console renderer implementation
 *
 * Translates the internal screen buffer into Win32 console output.
 * Each dirty row is converted to a CHAR_INFO array and written via
 * WriteConsoleOutputW, which is the fastest method available on
 * legacy Windows (2000+).
 *
 * Codepoint to WCHAR conversion:
 *   - BMP characters (U+0000 to U+FFFF): direct cast to WCHAR
 *   - Supplementary plane (U+10000+): replaced with U+FFFD since
 *     CHAR_INFO only holds a single WCHAR
 *   - Wide-trail cells (right half of CJK): rendered as space
 */

#include "winpatina_render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * Convert a Unicode codepoint to a single WCHAR for CHAR_INFO.
 * Supplementary plane characters cannot be represented in a single
 * WCHAR, so they are replaced with U+FFFD.
 */
static WCHAR codepoint_to_wchar(uint32_t cp)
{
    if (cp == 0) {
        return L' ';
    }
    if (cp <= 0xFFFF) {
        return (WCHAR)cp;
    }
    /* Supplementary plane: CHAR_INFO cannot hold surrogate pairs */
    return (WCHAR)0xFFFD;
}

/** Encode a Unicode codepoint as UTF-8. Returns bytes written (1-4). */
static int encode_utf8(uint32_t cp, char* out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    out[0] = '?';
    return 1;
}

/** Map Win32 foreground bits (0x0F) to ANSI 0-15 index. */
static int fg_bits_to_ansi(WORD fg_bits)
{
    static const WORD ansi_to_win32_fg[16] = {
        0x00, 0x04, 0x02, 0x06, 0x01, 0x05, 0x03, 0x07,
        0x08, 0x0C, 0x0A, 0x0E, 0x09, 0x0D, 0x0B, 0x0F
    };
    WORD fg = fg_bits & 0x0F;
    for (int i = 0; i < 16; i++) {
        if (ansi_to_win32_fg[i] == fg) {
            return i;
        }
    }
    return 7;
}

/** Map Win32 background bits (0xF0) to ANSI 0-15 index. */
static int bg_bits_to_ansi(WORD bg_bits)
{
    static const WORD ansi_to_win32_bg[16] = {
        0x00, 0x40, 0x20, 0x60, 0x10, 0x50, 0x30, 0x70,
        0x80, 0xC0, 0xA0, 0xE0, 0x90, 0xD0, 0xB0, 0xF0
    };
    WORD bg = bg_bits & 0xF0;
    for (int i = 0; i < 16; i++) {
        if (ansi_to_win32_bg[i] == bg) {
            return i;
        }
    }
    return 0;
}

/**
 * Build an SGR sequence from Win32 attributes.
 *
 * We use this for VT overlay passes where low-level attributes (notably
 * underline) are not consistently reflected by all hosts.
 */
static int sgr_from_attrs(WORD attrs, char* out, size_t out_cap)
{
    int fg = fg_bits_to_ansi(attrs & 0x0F);
    int bg = bg_bits_to_ansi(attrs & 0xF0);
    int pos = snprintf(out, out_cap, "\x1b[0");
    if (pos < 0 || (size_t)pos >= out_cap) return 0;

    if (attrs & 0x4000) {  /* COMMON_LVB_REVERSE_VIDEO */
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";7");
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    }
    if (attrs & 0x8000) {  /* COMMON_LVB_UNDERSCORE */
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";4");
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    }

    if (fg < 8) {
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";%d", 30 + fg);
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    } else {
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";%d", 90 + (fg - 8));
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    }

    if (bg < 8) {
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";%d", 40 + bg);
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    } else {
        int n = snprintf(out + pos, out_cap - (size_t)pos, ";%d", 100 + (bg - 8));
        if (n < 0 || (size_t)n >= out_cap - (size_t)pos) return 0;
        pos += n;
    }

    if ((size_t)pos + 2 > out_cap) return 0;
    out[pos++] = 'm';
    out[pos] = '\0';
    return pos;
}

/** Write bytes to console handle, ignoring short writes/failures. */
static void write_bytes(HANDLE handle, const char* data, int len)
{
    if (len <= 0) return;
    DWORD written = 0;
    WriteFile(handle, data, (DWORD)len, &written, NULL);
    (void)written;
}

/** Ensure the VT overlay scratch buffer can hold at least min_cap bytes. */
static bool ensure_overlay_capacity(WPRenderer* renderer, int min_cap)
{
    if (renderer == NULL || min_cap <= 0) {
        return false;
    }
    if (renderer->vt_overlay_cap >= min_cap && renderer->vt_overlay_buf != NULL) {
        return true;
    }

    int new_cap = (renderer->vt_overlay_cap > 0) ? renderer->vt_overlay_cap : 256;
    while (new_cap < min_cap) {
        if (new_cap > (INT_MAX / 2)) {
            new_cap = min_cap;
            break;
        }
        new_cap *= 2;
    }

    char* new_buf = (char*)realloc(renderer->vt_overlay_buf, (size_t)new_cap);
    if (new_buf == NULL) {
        return false;
    }

    renderer->vt_overlay_buf = new_buf;
    renderer->vt_overlay_cap = new_cap;
    return true;
}

/**
 * Overlay underlined cells using VT output.
 *
 * Some hosts do not faithfully represent COMMON_LVB_UNDERSCORE when content
 * is painted through WriteConsoleOutputW. Re-emitting only underlined spans
 * via VT keeps underline visible while preserving the fast cell renderer.
 */
static void paint_vt_underline_overlay(WPRenderer* renderer,
                                       WPScreenBuffer* screen)
{
    HANDLE console_handle = renderer->console_handle;
    const int width = screen->width;
    const int height = screen->height;

    for (int y = 0; y < height; y++) {
        if (!screen->full_repaint && !screen->dirty_rows[y]) {
            continue;
        }

        int x = 0;
        while (x < width) {
            WPScreenCell* cell = &screen->cells[y * width + x];
            if ((cell->attributes & 0x8000) == 0) {  /* COMMON_LVB_UNDERSCORE */
                x++;
                continue;
            }

            int start = x;
            WORD attrs = cell->attributes;
            while (x < width) {
                WPScreenCell* cur = &screen->cells[y * width + x];
                if ((cur->attributes & 0x8000) == 0 ||  /* COMMON_LVB_UNDERSCORE */
                    cur->attributes != attrs) {
                    break;
                }
                x++;
            }

            char seq[96];
            int n = snprintf(seq, sizeof(seq), "\x1b[%d;%dH", y + 1, start + 1);
            if (n > 0) {
                write_bytes(console_handle, seq, n);
            }

            char sgr[64];
            n = sgr_from_attrs(attrs, sgr, sizeof(sgr));
            if (n > 0) {
                write_bytes(console_handle, sgr, n);
            }

            /* Each cell contributes up to 4 UTF-8 bytes. */
            char* text = renderer->vt_overlay_buf;

            int pos = 0;
            for (int i = start; i < x; i++) {
                WPScreenCell* c = &screen->cells[y * width + i];
                uint32_t cp = c->wide_trail ? 0x20 : c->codepoint;
                if (cp == 0) cp = 0x20;
                if (cp > 0xFFFF) cp = 0xFFFD;  /* Match CHAR_INFO behaviour */
                pos += encode_utf8(cp, text + pos);
            }

            write_bytes(console_handle, text, pos);
        }
    }

    /* Leave terminal state in a known baseline before cursor restore. */
    write_bytes(console_handle, "\x1b[0m", 4);
}

/**
 * Build a CHAR_INFO row from screen buffer cells.
 */
static void build_row(CHAR_INFO* dest, WPScreenBuffer* screen, int y)
{
    for (int x = 0; x < screen->width; x++) {
        WPScreenCell* cell = &screen->cells[y * screen->width + x];

        if (cell->wide_trail) {
            /* Trailing half of a wide character: render as space
             * with the same attributes so the console handles it */
            dest[x].Char.UnicodeChar = L' ';
        } else {
            dest[x].Char.UnicodeChar = codepoint_to_wchar(cell->codepoint);
        }
        dest[x].Attributes = cell->attributes;
    }
}

/**
 * Emit a VT clear sequence directly to the console handle.
 *
 * Under conpty-backed terminals, this keeps the visible terminal in sync
 * on full repaints where WriteConsoleOutputW space fills can be missed.
 */
static void emit_vt_clear(HANDLE console_handle)
{
    static const char clear_seq[] = "\x1b[2J\x1b[H";
    DWORD written = 0;
    WriteFile(console_handle, clear_seq, (DWORD)(sizeof(clear_seq) - 1),
              &written, NULL);
    (void)written;
}

/*============================================================================
 * Lifecycle
 *============================================================================*/

bool wp_renderer_init(WPRenderer* renderer, HANDLE console_handle,
                      WPScreenBuffer* screen)
{
    if (renderer == NULL || screen == NULL) {
        return false;
    }

    memset(renderer, 0, sizeof(*renderer));
    renderer->console_handle = console_handle;
    renderer->screen = screen;
    renderer->cursor_state_valid = false;

    /* Allocate row buffer for the active screen width */
    WPScreenBuffer* active = wp_screen_active(screen);
    renderer->row_buf_width = active->width;
    renderer->row_buf = (CHAR_INFO*)malloc(
        sizeof(CHAR_INFO) * (size_t)renderer->row_buf_width);

    if (renderer->row_buf == NULL) {
        return false;
    }

    return true;
}

void wp_renderer_destroy(WPRenderer* renderer)
{
    if (renderer == NULL) {
        return;
    }

    free(renderer->row_buf);
    free(renderer->vt_overlay_buf);
    renderer->row_buf = NULL;
    renderer->vt_overlay_buf = NULL;
    renderer->row_buf_width = 0;
    renderer->vt_overlay_cap = 0;
}

/*============================================================================
 * Rendering
 *============================================================================*/

void wp_renderer_paint(WPRenderer* renderer)
{
    if (renderer == NULL || renderer->screen == NULL) {
        return;
    }

    WPScreenBuffer* active = wp_screen_active(renderer->screen);

    /* Reallocate row buffer if width changed */
    if (active->width != renderer->row_buf_width) {
        CHAR_INFO* new_buf = (CHAR_INFO*)realloc(
            renderer->row_buf,
            sizeof(CHAR_INFO) * (size_t)active->width);
        if (new_buf != NULL) {
            renderer->row_buf = new_buf;
            renderer->row_buf_width = active->width;
        } else {
            /* Allocation failed; skip this paint */
            return;
        }
    }

    /*
     * Get the current viewport position.
     *
     * Under conpty (Windows Terminal), the console buffer may be much
     * larger than the visible window, and the viewport scrolls down as
     * content is written.  WriteConsoleOutputW uses absolute buffer
     * coordinates, so we must offset all writes by the viewport's top
     * row to ensure we're writing to the VISIBLE area.
     */
    SHORT viewport_top = 0;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(renderer->console_handle, &csbi)) {
        viewport_top = csbi.srWindow.Top;
    }

    /*
     * Full repaint on VT-capable consoles: clear explicitly with VT first.
     * This avoids stale lines on some conpty paths where bulk space fills
     * through WriteConsoleOutputW are not reflected perfectly.
     */
    if (active->full_repaint && renderer->vt_output_enabled) {
        emit_vt_clear(renderer->console_handle);

        /* The clear/home may move the viewport; re-read the offset. */
        if (GetConsoleScreenBufferInfo(renderer->console_handle, &csbi)) {
            viewport_top = csbi.srWindow.Top;
        } else {
            viewport_top = 0;
        }
    }

    COORD buf_size;
    buf_size.X = (SHORT)active->width;
    buf_size.Y = 1;

    COORD buf_origin;
    buf_origin.X = 0;
    buf_origin.Y = 0;

    /* Paint each dirty row. */
    for (int y = 0; y < active->height; y++) {
        if (!active->full_repaint && !active->dirty_rows[y]) {
            continue;
        }

        build_row(renderer->row_buf, active, y);

        SMALL_RECT write_region;
        write_region.Left   = 0;
        write_region.Top    = (SHORT)(y + viewport_top);
        write_region.Right  = (SHORT)(active->width - 1);
        write_region.Bottom = (SHORT)(y + viewport_top);

        WriteConsoleOutputW(
            renderer->console_handle,
            renderer->row_buf,
            buf_size,
            buf_origin,
            &write_region);
    }

    if (renderer->vt_output_enabled &&
        ensure_overlay_capacity(renderer, active->width * 4)) {
        paint_vt_underline_overlay(renderer, active);
    }

    /* Mark all rows clean */
    wp_screen_mark_all_clean(active);

    /* Update cursor position. */
    COORD cursor_pos;
    cursor_pos.X = (SHORT)active->cursor.x;
    cursor_pos.Y = (SHORT)(active->cursor.y + viewport_top);

    if (!renderer->cursor_state_valid ||
        cursor_pos.X != renderer->last_cursor_pos.X ||
        cursor_pos.Y != renderer->last_cursor_pos.Y) {

        SetConsoleCursorPosition(renderer->console_handle, cursor_pos);
        renderer->last_cursor_pos = cursor_pos;
    }

    /* Update cursor visibility */
    if (!renderer->cursor_state_valid ||
        active->cursor.visible != renderer->last_cursor_visible) {

        CONSOLE_CURSOR_INFO cci;
        cci.dwSize = 25;  /* Default cursor size (25%) */
        cci.bVisible = active->cursor.visible ? TRUE : FALSE;
        SetConsoleCursorInfo(renderer->console_handle, &cci);
        renderer->last_cursor_visible = active->cursor.visible;
    }

    renderer->cursor_state_valid = true;
}

void wp_renderer_paint_all(WPRenderer* renderer)
{
    if (renderer == NULL || renderer->screen == NULL) {
        return;
    }

    WPScreenBuffer* active = wp_screen_active(renderer->screen);
    wp_screen_mark_all_dirty(active);
    wp_renderer_paint(renderer);
}

void wp_renderer_set_handle(WPRenderer* renderer, HANDLE handle)
{
    if (renderer != NULL) {
        renderer->console_handle = handle;
        renderer->cursor_state_valid = false;
    }
}

bool wp_renderer_resize(WPRenderer* renderer)
{
    if (renderer == NULL || renderer->screen == NULL) {
        return false;
    }

    WPScreenBuffer* active = wp_screen_active(renderer->screen);
    int new_width = active->width;

    if (new_width == renderer->row_buf_width) {
        return true;  /* No change needed */
    }

    CHAR_INFO* new_buf = (CHAR_INFO*)realloc(
        renderer->row_buf,
        sizeof(CHAR_INFO) * (size_t)new_width);

    if (new_buf == NULL) {
        return false;
    }

    renderer->row_buf = new_buf;
    renderer->row_buf_width = new_width;
    return true;
}
