/**
 * @file winpatina_screen.cpp
 * @brief Screen buffer management implementation
 *
 * Maintains an internal cell grid representing the console screen.
 * The VT parser callbacks write into this buffer; the Win32 renderer
 * reads from it. Row-level dirty tracking ensures only changed rows
 * are redrawn.
 *
 * Design notes:
 *
 * - Cells store Unicode codepoints (uint32_t). Conversion to UTF-16
 *   for Win32 APIs happens at render time, not here.
 *
 * - Scroll operations use memmove on row-sized blocks. For a typical
 *   80x25 or 120x30 screen this is fast enough; no ring-buffer
 *   optimisation is needed.
 *
 * - The alternate buffer is a separate WPScreenBuffer allocated on
 *   first use. Entering alternate saves the main cursor; leaving
 *   restores it and discards the alternate contents.
 */

#include "winpatina_screen.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Fill a range of cells with a character and attributes */
static void fill_cells(WPScreenCell* cells, int count, uint32_t codepoint,
                        WORD attrs)
{
    for (int i = 0; i < count; i++) {
        cells[i].codepoint = codepoint;
        cells[i].attributes = attrs;
        cells[i].wide_trail = false;
    }
}

/** Get a pointer to the start of row y */
static WPScreenCell* row_ptr(WPScreenBuffer* screen, int y)
{
    return &screen->cells[y * screen->width];
}

/** Clamp a value to [min, max] */
static int clamp(int value, int min_val, int max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/*============================================================================
 * Lifecycle
 *============================================================================*/

WPScreenBuffer* wp_screen_create(int width, int height, WORD default_attrs)
{
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    WPScreenBuffer* screen = (WPScreenBuffer*)calloc(1, sizeof(WPScreenBuffer));
    if (screen == NULL) {
        return NULL;
    }

    screen->cells = (WPScreenCell*)malloc(
        sizeof(WPScreenCell) * (size_t)width * (size_t)height);
    if (screen->cells == NULL) {
        free(screen);
        return NULL;
    }

    screen->dirty_rows = (bool*)calloc((size_t)height, sizeof(bool));
    if (screen->dirty_rows == NULL) {
        free(screen->cells);
        free(screen);
        return NULL;
    }

    screen->width = width;
    screen->height = height;
    screen->default_attrs = default_attrs;
    screen->current_attrs = default_attrs;

    /* Fill all cells with spaces */
    fill_cells(screen->cells, width * height, 0x20, default_attrs);

    /* Cursor starts at top-left, visible */
    screen->cursor.x = 0;
    screen->cursor.y = 0;
    screen->cursor.visible = true;
    screen->cursor.pending_wrap = false;

    /* Scroll region covers the entire screen */
    screen->scroll_top = 0;
    screen->scroll_bottom = height - 1;

    /* Mark everything dirty for initial render */
    screen->full_repaint = true;

    return screen;
}

void wp_screen_destroy(WPScreenBuffer* screen)
{
    if (screen == NULL) {
        return;
    }

    /* Destroy alternate buffer if it exists */
    if (screen->alternate != NULL) {
        wp_screen_destroy(screen->alternate);
    }

    free(screen->dirty_rows);
    free(screen->cells);
    free(screen);
}

/*============================================================================
 * Cell Access
 *============================================================================*/

WPScreenCell* wp_screen_cell_at(WPScreenBuffer* screen, int x, int y)
{
    if (screen == NULL) {
        return NULL;
    }
    if (x < 0 || x >= screen->width || y < 0 || y >= screen->height) {
        return NULL;
    }
    return &screen->cells[y * screen->width + x];
}

void wp_screen_put_char(WPScreenBuffer* screen, uint32_t codepoint)
{
    if (screen == NULL) {
        return;
    }

    /*
     * Handle deferred wrap: if a previous character landed on the
     * rightmost column, the wrap was deferred. Now that we have
     * another character to write, perform the wrap first.
     */
    if (screen->cursor.pending_wrap) {
        screen->cursor.pending_wrap = false;
        screen->cursor.x = 0;
        screen->cursor.y++;

        /* Scroll if we've gone past the bottom of the scroll region */
        if (screen->cursor.y > screen->scroll_bottom) {
            screen->cursor.y = screen->scroll_bottom;
            wp_screen_scroll(screen, 1);
        }
    }

    /* Write the character at the cursor position */
    WPScreenCell* cell = wp_screen_cell_at(screen, screen->cursor.x,
                                            screen->cursor.y);
    if (cell != NULL) {
        cell->codepoint = codepoint;
        cell->attributes = screen->current_attrs;
        cell->wide_trail = false;
        wp_screen_mark_dirty(screen, screen->cursor.y);
    }

    /* Advance cursor */
    screen->cursor.x++;

    /* If we've reached the right edge, defer the wrap */
    if (screen->cursor.x >= screen->width) {
        screen->cursor.x = screen->width - 1;
        screen->cursor.pending_wrap = true;
    }
}

/*============================================================================
 * Erase Operations
 *============================================================================*/

void wp_screen_erase_display(WPScreenBuffer* screen, int mode)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.pending_wrap = false;

    switch (mode) {
    case 0:
        /* Cursor to end of screen */
        /* Rest of current line */
        fill_cells(
            wp_screen_cell_at(screen, screen->cursor.x, screen->cursor.y),
            screen->width - screen->cursor.x,
            0x20, screen->default_attrs);
        wp_screen_mark_dirty(screen, screen->cursor.y);

        /* All lines below cursor */
        for (int y = screen->cursor.y + 1; y < screen->height; y++) {
            fill_cells(row_ptr(screen, y), screen->width,
                       0x20, screen->default_attrs);
            wp_screen_mark_dirty(screen, y);
        }
        break;

    case 1:
        /* Start of screen to cursor */
        /* All lines above cursor */
        for (int y = 0; y < screen->cursor.y; y++) {
            fill_cells(row_ptr(screen, y), screen->width,
                       0x20, screen->default_attrs);
            wp_screen_mark_dirty(screen, y);
        }

        /* Start of current line to cursor (inclusive) */
        fill_cells(row_ptr(screen, screen->cursor.y),
                   screen->cursor.x + 1,
                   0x20, screen->default_attrs);
        wp_screen_mark_dirty(screen, screen->cursor.y);
        break;

    case 2:
    case 3:
        /* Entire screen */
        fill_cells(screen->cells, screen->width * screen->height,
                   0x20, screen->default_attrs);
        wp_screen_mark_all_dirty(screen);
        break;
    }
}

void wp_screen_erase_line(WPScreenBuffer* screen, int mode)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.pending_wrap = false;
    int y = screen->cursor.y;

    switch (mode) {
    case 0:
        /* Cursor to end of line */
        fill_cells(
            wp_screen_cell_at(screen, screen->cursor.x, y),
            screen->width - screen->cursor.x,
            0x20, screen->default_attrs);
        break;

    case 1:
        /* Start of line to cursor (inclusive) */
        fill_cells(row_ptr(screen, y),
                   screen->cursor.x + 1,
                   0x20, screen->default_attrs);
        break;

    case 2:
        /* Entire line */
        fill_cells(row_ptr(screen, y), screen->width,
                   0x20, screen->default_attrs);
        break;
    }

    wp_screen_mark_dirty(screen, y);
}

/*============================================================================
 * Cursor Movement
 *============================================================================*/

void wp_screen_set_cursor(WPScreenBuffer* screen, int x, int y)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.x = clamp(x, 0, screen->width - 1);
    screen->cursor.y = clamp(y, 0, screen->height - 1);
    screen->cursor.pending_wrap = false;
}

void wp_screen_move_cursor(WPScreenBuffer* screen, int dx, int dy)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.x = clamp(screen->cursor.x + dx, 0, screen->width - 1);
    screen->cursor.y = clamp(screen->cursor.y + dy, 0, screen->height - 1);
    screen->cursor.pending_wrap = false;
}

void wp_screen_save_cursor(WPScreenBuffer* screen)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.saved_x = screen->cursor.x;
    screen->cursor.saved_y = screen->cursor.y;
    screen->cursor.saved_attrs = screen->current_attrs;
}

void wp_screen_restore_cursor(WPScreenBuffer* screen)
{
    if (screen == NULL) {
        return;
    }

    screen->cursor.x = clamp(screen->cursor.saved_x, 0, screen->width - 1);
    screen->cursor.y = clamp(screen->cursor.saved_y, 0, screen->height - 1);
    screen->cursor.pending_wrap = false;
    screen->current_attrs = screen->cursor.saved_attrs;
}

/*============================================================================
 * Scrolling
 *============================================================================*/

void wp_screen_set_scroll_region(WPScreenBuffer* screen, int top, int bottom)
{
    if (screen == NULL) {
        return;
    }

    top = clamp(top, 0, screen->height - 1);
    bottom = clamp(bottom, 0, screen->height - 1);

    /* Top must be above bottom */
    if (top >= bottom) {
        return;
    }

    screen->scroll_top = top;
    screen->scroll_bottom = bottom;

    /* DECSTBM moves cursor to home position */
    screen->cursor.x = 0;
    screen->cursor.y = 0;
    screen->cursor.pending_wrap = false;
}

void wp_screen_scroll(WPScreenBuffer* screen, int lines)
{
    if (screen == NULL || lines == 0) {
        return;
    }

    int top = screen->scroll_top;
    int bottom = screen->scroll_bottom;
    int region_height = bottom - top + 1;

    /* Clamp scroll amount to region height */
    if (lines > region_height) lines = region_height;
    if (lines < -region_height) lines = -region_height;

    size_t row_bytes = sizeof(WPScreenCell) * (size_t)screen->width;

    if (lines > 0) {
        /* Scroll up: content moves up, blank lines appear at bottom */
        int rows_to_move = region_height - lines;
        if (rows_to_move > 0) {
            memmove(
                row_ptr(screen, top),
                row_ptr(screen, top + lines),
                row_bytes * (size_t)rows_to_move);
        }

        /* Clear the new blank rows at the bottom */
        for (int y = bottom - lines + 1; y <= bottom; y++) {
            fill_cells(row_ptr(screen, y), screen->width,
                       0x20, screen->default_attrs);
        }
    }
    else {
        /* Scroll down: content moves down, blank lines appear at top */
        int down = -lines;
        int rows_to_move = region_height - down;
        if (rows_to_move > 0) {
            memmove(
                row_ptr(screen, top + down),
                row_ptr(screen, top),
                row_bytes * (size_t)rows_to_move);
        }

        /* Clear the new blank rows at the top */
        for (int y = top; y < top + down; y++) {
            fill_cells(row_ptr(screen, y), screen->width,
                       0x20, screen->default_attrs);
        }
    }

    /* Mark all rows in the scroll region as dirty */
    for (int y = top; y <= bottom; y++) {
        screen->dirty_rows[y] = true;
    }
}

void wp_screen_insert_lines(WPScreenBuffer* screen, int count)
{
    if (screen == NULL || count <= 0) {
        return;
    }

    int y = screen->cursor.y;

    /* Only operates within the scroll region */
    if (y < screen->scroll_top || y > screen->scroll_bottom) {
        return;
    }

    /* Temporarily narrow the scroll region to [cursor_y .. scroll_bottom] */
    int saved_top = screen->scroll_top;
    screen->scroll_top = y;
    wp_screen_scroll(screen, -count);  /* Scroll down = insert blank lines */
    screen->scroll_top = saved_top;

    screen->cursor.x = 0;
    screen->cursor.pending_wrap = false;
}

void wp_screen_delete_lines(WPScreenBuffer* screen, int count)
{
    if (screen == NULL || count <= 0) {
        return;
    }

    int y = screen->cursor.y;

    if (y < screen->scroll_top || y > screen->scroll_bottom) {
        return;
    }

    int saved_top = screen->scroll_top;
    screen->scroll_top = y;
    wp_screen_scroll(screen, count);  /* Scroll up = delete lines */
    screen->scroll_top = saved_top;

    screen->cursor.x = 0;
    screen->cursor.pending_wrap = false;
}

/*============================================================================
 * Character Insert/Delete
 *============================================================================*/

void wp_screen_insert_chars(WPScreenBuffer* screen, int count)
{
    if (screen == NULL || count <= 0) {
        return;
    }

    int x = screen->cursor.x;
    int y = screen->cursor.y;
    int w = screen->width;

    /* Clamp count so we don't go past the end of the line */
    if (count > w - x) {
        count = w - x;
    }

    WPScreenCell* row = row_ptr(screen, y);
    int chars_to_move = w - x - count;

    if (chars_to_move > 0) {
        memmove(&row[x + count], &row[x],
                sizeof(WPScreenCell) * (size_t)chars_to_move);
    }

    /* Fill the inserted positions with blanks */
    fill_cells(&row[x], count, 0x20, screen->default_attrs);
    wp_screen_mark_dirty(screen, y);
    screen->cursor.pending_wrap = false;
}

void wp_screen_delete_chars(WPScreenBuffer* screen, int count)
{
    if (screen == NULL || count <= 0) {
        return;
    }

    int x = screen->cursor.x;
    int y = screen->cursor.y;
    int w = screen->width;

    if (count > w - x) {
        count = w - x;
    }

    WPScreenCell* row = row_ptr(screen, y);
    int chars_to_move = w - x - count;

    if (chars_to_move > 0) {
        memmove(&row[x], &row[x + count],
                sizeof(WPScreenCell) * (size_t)chars_to_move);
    }

    /* Fill the vacated positions at the right edge with blanks */
    fill_cells(&row[w - count], count, 0x20, screen->default_attrs);
    wp_screen_mark_dirty(screen, y);
    screen->cursor.pending_wrap = false;
}

/*============================================================================
 * Dirty Tracking
 *============================================================================*/

void wp_screen_mark_dirty(WPScreenBuffer* screen, int y)
{
    if (screen != NULL && y >= 0 && y < screen->height) {
        screen->dirty_rows[y] = true;
    }
}

void wp_screen_mark_all_clean(WPScreenBuffer* screen)
{
    if (screen == NULL) {
        return;
    }

    memset(screen->dirty_rows, 0, sizeof(bool) * (size_t)screen->height);
    screen->full_repaint = false;
}

void wp_screen_mark_all_dirty(WPScreenBuffer* screen)
{
    if (screen == NULL) {
        return;
    }

    memset(screen->dirty_rows, 1, sizeof(bool) * (size_t)screen->height);
    screen->full_repaint = true;
}

/*============================================================================
 * Alternate Buffer
 *============================================================================*/

void wp_screen_enter_alternate(WPScreenBuffer* screen)
{
    if (screen == NULL || screen->using_alternate) {
        return;
    }

    /* Save cursor state on the main buffer */
    wp_screen_save_cursor(screen);

    /* Create alternate buffer if it doesn't exist */
    if (screen->alternate == NULL) {
        screen->alternate = wp_screen_create(
            screen->width, screen->height, screen->default_attrs);
        if (screen->alternate == NULL) {
            return;  /* Allocation failure - stay on main buffer */
        }
    }
    else {
        /* Re-entering alternate: clear it */
        fill_cells(screen->alternate->cells,
                   screen->alternate->width * screen->alternate->height,
                   0x20, screen->default_attrs);
        screen->alternate->cursor.x = 0;
        screen->alternate->cursor.y = 0;
        screen->alternate->cursor.visible = true;
        screen->alternate->cursor.pending_wrap = false;
        screen->alternate->current_attrs = screen->default_attrs;
        screen->alternate->scroll_top = 0;
        screen->alternate->scroll_bottom = screen->alternate->height - 1;
        wp_screen_mark_all_dirty(screen->alternate);
    }

    screen->using_alternate = true;
}

void wp_screen_leave_alternate(WPScreenBuffer* screen)
{
    if (screen == NULL || !screen->using_alternate) {
        return;
    }

    screen->using_alternate = false;

    /* Restore cursor from the main buffer's saved state */
    wp_screen_restore_cursor(screen);

    /* Mark everything dirty so the main buffer is fully redrawn */
    wp_screen_mark_all_dirty(screen);
}

/*============================================================================
 * Resize
 *============================================================================*/

bool wp_screen_resize(WPScreenBuffer* screen, int new_width, int new_height)
{
    if (screen == NULL || new_width <= 0 || new_height <= 0) {
        return false;
    }

    /* Same size - nothing to do */
    if (new_width == screen->width && new_height == screen->height) {
        return true;
    }

    /* Allocate new cells and dirty rows */
    WPScreenCell* new_cells = (WPScreenCell*)malloc(
        sizeof(WPScreenCell) * (size_t)new_width * (size_t)new_height);
    if (new_cells == NULL) {
        return false;
    }

    bool* new_dirty = (bool*)calloc((size_t)new_height, sizeof(bool));
    if (new_dirty == NULL) {
        free(new_cells);
        return false;
    }

    /* Fill new cells with defaults */
    fill_cells(new_cells, new_width * new_height, 0x20, screen->default_attrs);

    /* Copy existing content (as much as fits) */
    int copy_w = (new_width < screen->width) ? new_width : screen->width;
    int copy_h = (new_height < screen->height) ? new_height : screen->height;

    for (int y = 0; y < copy_h; y++) {
        memcpy(
            &new_cells[y * new_width],
            &screen->cells[y * screen->width],
            sizeof(WPScreenCell) * (size_t)copy_w);
    }

    /* Swap in the new buffers */
    free(screen->cells);
    free(screen->dirty_rows);
    screen->cells = new_cells;
    screen->dirty_rows = new_dirty;
    screen->width = new_width;
    screen->height = new_height;

    /* Clamp cursor to new bounds */
    screen->cursor.x = clamp(screen->cursor.x, 0, new_width - 1);
    screen->cursor.y = clamp(screen->cursor.y, 0, new_height - 1);
    screen->cursor.pending_wrap = false;

    /* Reset scroll region to full screen */
    screen->scroll_top = 0;
    screen->scroll_bottom = new_height - 1;

    /* Force full repaint */
    wp_screen_mark_all_dirty(screen);

    /* Resize alternate buffer too if it exists */
    if (screen->alternate != NULL) {
        wp_screen_resize(screen->alternate, new_width, new_height);
    }

    return true;
}
