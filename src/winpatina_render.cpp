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
    renderer->row_buf = NULL;
    renderer->row_buf_width = 0;
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

    COORD buf_size;
    buf_size.X = (SHORT)active->width;
    buf_size.Y = 1;

    COORD buf_origin;
    buf_origin.X = 0;
    buf_origin.Y = 0;

    /* Paint each dirty row */
    for (int y = 0; y < active->height; y++) {
        if (!active->full_repaint && !active->dirty_rows[y]) {
            continue;
        }

        build_row(renderer->row_buf, active, y);

        SMALL_RECT write_region;
        write_region.Left   = 0;
        write_region.Top    = (SHORT)y;
        write_region.Right  = (SHORT)(active->width - 1);
        write_region.Bottom = (SHORT)y;

        WriteConsoleOutputW(
            renderer->console_handle,
            renderer->row_buf,
            buf_size,
            buf_origin,
            &write_region);
    }

    /* Mark all rows clean */
    wp_screen_mark_all_clean(active);

    /* Update cursor position */
    COORD cursor_pos;
    cursor_pos.X = (SHORT)active->cursor.x;
    cursor_pos.Y = (SHORT)active->cursor.y;

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
