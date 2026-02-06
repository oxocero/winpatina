/**
 * @file winpatina_screen.h
 * @brief Screen buffer management - internal header
 *
 * Maintains an internal representation of the console screen as a grid
 * of cells, each with a character and Win32 console attributes. This
 * buffer is the bridge between the VT parser (which writes to it) and
 * the Win32 renderer (which reads from it).
 *
 * Key features:
 * - Row-level dirty tracking for efficient partial redraws
 * - Alternate screen buffer support (CSI ? 1049 h/l)
 * - Scroll region support (DECSTBM)
 * - Cursor state management
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_SCREEN_H
#define WINPATINA_SCREEN_H

#include <stdint.h>
#include <stdbool.h>

/* Windows headers for WORD type and console attributes */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Screen Cell
 *============================================================================*/

/**
 * @brief A single cell in the screen buffer
 *
 * Each cell holds one character (as a Unicode codepoint) and Win32
 * console attributes. Wide characters (CJK, emoji) occupy two cells:
 * the first holds the codepoint and the second is marked as a
 * trailing cell with codepoint 0.
 */
typedef struct {
    /**
     * Unicode codepoint for this cell.
     * 0 indicates an empty cell or the trailing half of a wide character.
     */
    uint32_t codepoint;

    /** Win32 console attributes (foreground, background, LVB flags) */
    WORD attributes;

    /**
     * True if this cell is the trailing (right) half of a wide character.
     * The renderer skips these cells and draws the wide character from
     * the leading cell.
     */
    bool wide_trail;
} WPScreenCell;

/*============================================================================
 * Cursor State
 *============================================================================*/

/**
 * @brief Cursor position and appearance
 */
typedef struct {
    int x;              /**< Column (0-based) */
    int y;              /**< Row (0-based) */
    bool visible;       /**< Whether the cursor is displayed */

    /**
     * Deferred line wrap flag.
     *
     * When the cursor reaches the right margin after writing a character,
     * it does NOT wrap immediately. Instead this flag is set. The wrap
     * occurs on the NEXT printable character. This allows the rightmost
     * column to be filled without spuriously starting a new line.
     */
    bool pending_wrap;

    /** Saved cursor position (DECSC / ESC 7) */
    int saved_x;
    int saved_y;
    WORD saved_attrs;   /**< Attributes at the time of save */
} WPCursorState;

/*============================================================================
 * Screen Buffer
 *============================================================================*/

/**
 * @brief Screen buffer state
 *
 * Represents the full state of one screen (main or alternate).
 * Created with wp_screen_create(), destroyed with wp_screen_destroy().
 */
typedef struct WPScreenBuffer WPScreenBuffer;

struct WPScreenBuffer {
    /** Cell grid, row-major order: cells[y * width + x] */
    WPScreenCell* cells;

    int width;          /**< Number of columns */
    int height;         /**< Number of rows */

    /** Cursor state */
    WPCursorState cursor;

    /** Current text attributes applied to new characters */
    WORD current_attrs;

    /** Default attributes (used for erase/clear operations) */
    WORD default_attrs;

    /*--- Scroll region (DECSTBM) ---*/

    /** Top row of scroll region (0-based, inclusive) */
    int scroll_top;

    /** Bottom row of scroll region (0-based, inclusive) */
    int scroll_bottom;

    /*--- Dirty tracking ---*/

    /**
     * Per-row dirty flags.
     * dirty_rows[y] is true if row y has changed since last render.
     * Allocated as an array of `height` bools.
     */
    bool* dirty_rows;

    /** True if a full repaint is needed (e.g., after resize) */
    bool full_repaint;

    /*--- Alternate buffer ---*/

    /**
     * Pointer to the alternate screen buffer (NULL if this IS the
     * alternate, or if alternate hasn't been created yet).
     */
    WPScreenBuffer* alternate;

    /** True if we have switched to the alternate buffer */
    bool using_alternate;
};

/*============================================================================
 * Lifecycle
 *============================================================================*/

/**
 * @brief Create a new screen buffer
 *
 * Allocates and initialises a screen buffer of the given dimensions.
 * All cells are set to space (0x20) with the specified default attributes.
 *
 * @param width          Number of columns
 * @param height         Number of rows
 * @param default_attrs  Default Win32 attributes for new/erased cells
 * @return Newly allocated screen buffer, or NULL on failure
 */
WPScreenBuffer* wp_screen_create(int width, int height, WORD default_attrs);

/**
 * @brief Destroy a screen buffer and free all memory
 *
 * Also destroys the alternate buffer if one exists. Safe to call with NULL.
 *
 * @param screen  Screen buffer to destroy
 */
void wp_screen_destroy(WPScreenBuffer* screen);

/*============================================================================
 * Cell Access
 *============================================================================*/

/**
 * @brief Get a pointer to a cell at (x, y)
 *
 * Returns NULL if coordinates are out of bounds.
 *
 * @param screen  Screen buffer
 * @param x       Column (0-based)
 * @param y       Row (0-based)
 * @return Pointer to the cell, or NULL
 */
WPScreenCell* wp_screen_cell_at(WPScreenBuffer* screen, int x, int y);

/**
 * @brief Write a codepoint at the cursor position
 *
 * Places the character using the screen's current_attrs, advances the
 * cursor one position to the right. If the cursor reaches the right
 * edge, it wraps to the next line (scrolling if at the bottom of the
 * scroll region).
 *
 * @param screen     Screen buffer
 * @param codepoint  Unicode codepoint to write
 */
void wp_screen_put_char(WPScreenBuffer* screen, uint32_t codepoint);

/*============================================================================
 * Erase Operations
 *============================================================================*/

/**
 * @brief Erase in display (ED - CSI J)
 *
 * @param screen  Screen buffer
 * @param mode    0 = cursor to end, 1 = start to cursor, 2 = entire screen,
 *                3 = entire screen + scrollback (treated as 2)
 */
void wp_screen_erase_display(WPScreenBuffer* screen, int mode);

/**
 * @brief Erase in line (EL - CSI K)
 *
 * @param screen  Screen buffer
 * @param mode    0 = cursor to end of line, 1 = start to cursor, 2 = entire line
 */
void wp_screen_erase_line(WPScreenBuffer* screen, int mode);

/*============================================================================
 * Cursor Movement
 *============================================================================*/

/**
 * @brief Set cursor position (CUP - CSI H)
 *
 * Coordinates are clamped to the screen bounds.
 *
 * @param screen  Screen buffer
 * @param x       Column (0-based)
 * @param y       Row (0-based)
 */
void wp_screen_set_cursor(WPScreenBuffer* screen, int x, int y);

/**
 * @brief Move cursor relative to current position
 *
 * @param screen  Screen buffer
 * @param dx      Horizontal offset (positive = right)
 * @param dy      Vertical offset (positive = down)
 */
void wp_screen_move_cursor(WPScreenBuffer* screen, int dx, int dy);

/**
 * @brief Save cursor position and attributes (DECSC / ESC 7)
 */
void wp_screen_save_cursor(WPScreenBuffer* screen);

/**
 * @brief Restore cursor position and attributes (DECRC / ESC 8)
 */
void wp_screen_restore_cursor(WPScreenBuffer* screen);

/*============================================================================
 * Scrolling
 *============================================================================*/

/**
 * @brief Set the scroll region (DECSTBM - CSI r)
 *
 * Defines the range of rows affected by scroll operations and line
 * feeds at the bottom of the region. Values are clamped to screen bounds.
 *
 * @param screen  Screen buffer
 * @param top     First row of region (0-based, inclusive)
 * @param bottom  Last row of region (0-based, inclusive)
 */
void wp_screen_set_scroll_region(WPScreenBuffer* screen, int top, int bottom);

/**
 * @brief Scroll the scroll region up or down
 *
 * Positive lines = scroll up (content moves up, blank lines appear at bottom).
 * Negative lines = scroll down (content moves down, blank lines at top).
 *
 * @param screen  Screen buffer
 * @param lines   Number of lines to scroll (positive = up)
 */
void wp_screen_scroll(WPScreenBuffer* screen, int lines);

/**
 * @brief Insert blank lines at the cursor row (IL - CSI L)
 *
 * Pushes existing lines down within the scroll region.
 *
 * @param screen  Screen buffer
 * @param count   Number of lines to insert
 */
void wp_screen_insert_lines(WPScreenBuffer* screen, int count);

/**
 * @brief Delete lines at the cursor row (DL - CSI M)
 *
 * Pulls lines up within the scroll region; blank lines appear at bottom.
 *
 * @param screen  Screen buffer
 * @param count   Number of lines to delete
 */
void wp_screen_delete_lines(WPScreenBuffer* screen, int count);

/*============================================================================
 * Character Insert/Delete
 *============================================================================*/

/**
 * @brief Insert blank characters at the cursor (ICH - CSI @)
 *
 * Shifts existing characters to the right; characters pushed past
 * the right edge are lost.
 *
 * @param screen  Screen buffer
 * @param count   Number of characters to insert
 */
void wp_screen_insert_chars(WPScreenBuffer* screen, int count);

/**
 * @brief Delete characters at the cursor (DCH - CSI P)
 *
 * Shifts characters to the left; blank characters appear at the right edge.
 *
 * @param screen  Screen buffer
 * @param count   Number of characters to delete
 */
void wp_screen_delete_chars(WPScreenBuffer* screen, int count);

/*============================================================================
 * Dirty Tracking
 *============================================================================*/

/**
 * @brief Mark a single row as dirty
 */
void wp_screen_mark_dirty(WPScreenBuffer* screen, int y);

/**
 * @brief Mark all rows as clean (after a successful render)
 */
void wp_screen_mark_all_clean(WPScreenBuffer* screen);

/**
 * @brief Mark all rows as dirty (force full repaint)
 */
void wp_screen_mark_all_dirty(WPScreenBuffer* screen);

/*============================================================================
 * Alternate Buffer
 *============================================================================*/

/**
 * @brief Switch to the alternate screen buffer (CSI ? 1049 h)
 *
 * Saves the cursor, creates a fresh alternate buffer (if needed),
 * and switches to it. If already using the alternate buffer, this
 * is a no-op.
 *
 * @param screen  Screen buffer (must be the main buffer)
 */
void wp_screen_enter_alternate(WPScreenBuffer* screen);

/**
 * @brief Switch back to the main screen buffer (CSI ? 1049 l)
 *
 * Restores the cursor and switches back. The alternate buffer's
 * contents are discarded.
 *
 * @param screen  Screen buffer (must be the main buffer)
 */
void wp_screen_leave_alternate(WPScreenBuffer* screen);

/*============================================================================
 * Resize
 *============================================================================*/

/**
 * @brief Resize the screen buffer
 *
 * Preserves as much content as possible. New cells are filled with
 * the default attributes. The cursor is clamped to the new bounds.
 * Marks the entire screen as dirty.
 *
 * @param screen      Screen buffer
 * @param new_width   New number of columns
 * @param new_height  New number of rows
 * @return true on success, false on allocation failure
 */
bool wp_screen_resize(WPScreenBuffer* screen, int new_width, int new_height);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_SCREEN_H */
