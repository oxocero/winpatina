/**
 * @file winpatina_render.h
 * @brief Win32 console renderer - internal header
 *
 * Reads from the screen buffer and writes to a Win32 console handle
 * using WriteConsoleOutputW. Only dirty rows are redrawn for efficiency.
 *
 * The renderer converts uint32_t codepoints to UTF-16 WCHAR values at
 * paint time. Supplementary-plane characters (U+10000+) are rendered
 * as U+FFFD since CHAR_INFO only holds a single WCHAR.
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_RENDER_H
#define WINPATINA_RENDER_H

#include "winpatina_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Renderer Structure
 *============================================================================*/

/**
 * @brief Win32 console renderer state
 *
 * Holds the console handle, a reference to the screen buffer, and a
 * pre-allocated row buffer for building CHAR_INFO arrays.
 */
typedef struct {
    /** Win32 console output handle (from CreateConsoleScreenBuffer or
     *  GetStdHandle) */
    HANDLE console_handle;

    /** Main screen buffer to render from (active buffer resolved internally) */
    WPScreenBuffer* screen;

    /** Pre-allocated CHAR_INFO buffer, sized for row_buf_width * row_buf_rows */
    CHAR_INFO* row_buf;

    /** Width of the allocated row buffer */
    int row_buf_width;

    /** Number of rows currently allocated in row_buf */
    int row_buf_rows;

    /** Cached cursor position to minimise API calls */
    COORD last_cursor_pos;

    /** Cached logical cursor X (without viewport offset) */
    int last_cursor_x;

    /** Cached logical cursor Y (without viewport offset) */
    int last_cursor_y;

    /** Cached cursor visibility */
    bool last_cursor_visible;

    /** Whether cached cursor state is valid */
    bool cursor_state_valid;

    /** Whether VT output is enabled on the console handle (conpty/WT).
     *  When true, full_repaint uses a direct VT clear sequence instead of
     *  relying on WriteConsoleOutputW-based clearing which conpty's diff
     *  algorithm may not translate correctly. */
    bool vt_output_enabled;

    /** Reusable UTF-8 scratch buffer for VT underline overlay runs. */
    char* vt_overlay_buf;

    /** Capacity of vt_overlay_buf in bytes. */
    int vt_overlay_cap;
} WPRenderer;

/*============================================================================
 * Lifecycle
 *============================================================================*/

/**
 * @brief Initialise the renderer
 *
 * Allocates the internal row buffer and prepares the renderer for
 * painting. The screen buffer must already be created.
 *
 * @param renderer        Renderer to initialise
 * @param console_handle  Win32 console output handle
 * @param screen          Main screen buffer
 * @return true on success, false on allocation failure
 */
bool wp_renderer_init(WPRenderer* renderer, HANDLE console_handle,
                      WPScreenBuffer* screen);

/**
 * @brief Destroy the renderer and free internal buffers
 *
 * Does not destroy the screen buffer or close the console handle.
 *
 * @param renderer  Renderer to destroy
 */
void wp_renderer_destroy(WPRenderer* renderer);

/*============================================================================
 * Rendering
 *============================================================================*/

/**
 * @brief Paint dirty rows to the console
 *
 * Iterates over the active screen buffer's dirty rows and writes them
 * to the console via WriteConsoleOutputW. Also updates the cursor
 * position and visibility if they have changed.
 *
 * After painting, all rows are marked clean.
 *
 * @param renderer  Renderer instance
 */
void wp_renderer_paint(WPRenderer* renderer);

/**
 * @brief Force a full repaint of the entire screen
 *
 * Marks all rows dirty and then paints. Useful after console resize
 * or when the console contents may have been corrupted.
 *
 * @param renderer  Renderer instance
 */
void wp_renderer_paint_all(WPRenderer* renderer);

/**
 * @brief Update the console handle
 *
 * Call this when the console handle changes (e.g., after creating a
 * new console screen buffer).
 *
 * @param renderer  Renderer instance
 * @param handle    New console output handle
 */
void wp_renderer_set_handle(WPRenderer* renderer, HANDLE handle);

/**
 * @brief Reallocate internal buffers after a screen resize
 *
 * Must be called after wp_screen_resize() if the width changed.
 *
 * @param renderer  Renderer instance
 * @return true on success, false on allocation failure
 */
bool wp_renderer_resize(WPRenderer* renderer);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_RENDER_H */
