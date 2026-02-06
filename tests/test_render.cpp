/**
 * @file test_render.cpp
 * @brief Unit tests for the Win32 console renderer
 *
 * The renderer calls Win32 console APIs (WriteConsoleOutputW, etc.)
 * which require a real console handle. Tests use INVALID_HANDLE_VALUE
 * to verify state management without actual console output — the API
 * calls will fail silently but internal state (dirty tracking, cursor
 * caching, row buffer) should still be managed correctly.
 */

#include "test_harness.h"
#include "../src/winpatina_render.h"

/*============================================================================
 * Test Helpers
 *============================================================================*/

#define DEFAULT_ATTRS 0x07  /* White on black */

/** Create a screen + renderer pair. Use INVALID_HANDLE_VALUE for console. */
struct RenderFixture {
    WPScreenBuffer* screen;
    WPRenderer renderer;
    bool init_ok;

    RenderFixture(int width = 80, int height = 24) {
        screen = wp_screen_create(width, height, DEFAULT_ATTRS);
        init_ok = wp_renderer_init(&renderer, INVALID_HANDLE_VALUE, screen);
    }

    ~RenderFixture() {
        wp_renderer_destroy(&renderer);
        wp_screen_destroy(screen);
    }
};

/*============================================================================
 * Tests - Lifecycle
 *============================================================================*/

TEST(render_init_destroy) {
    RenderFixture f;

    ASSERT_TRUE(f.init_ok);
    ASSERT_TRUE(f.renderer.row_buf != NULL);
    ASSERT_EQ(f.renderer.row_buf_width, 80);
    ASSERT_EQ(f.renderer.screen, f.screen);
}

TEST(render_init_null_params) {
    WPRenderer r;
    ASSERT_FALSE(wp_renderer_init(&r, INVALID_HANDLE_VALUE, NULL));
    ASSERT_FALSE(wp_renderer_init(NULL, INVALID_HANDLE_VALUE, NULL));
}

TEST(render_destroy_null_safe) {
    /* Should not crash */
    wp_renderer_destroy(NULL);
    ASSERT_TRUE(true);
}

TEST(render_init_small_screen) {
    RenderFixture f(5, 3);

    ASSERT_TRUE(f.init_ok);
    ASSERT_EQ(f.renderer.row_buf_width, 5);
}

/*============================================================================
 * Tests - Paint (state management, dirty tracking)
 *============================================================================*/

TEST(render_paint_clears_dirty) {
    RenderFixture f(10, 5);

    /* Screen starts with full_repaint = true */
    ASSERT_TRUE(f.screen->full_repaint);

    /* Paint should clear dirty flags even with invalid handle */
    wp_renderer_paint(&f.renderer);

    ASSERT_FALSE(f.screen->full_repaint);
    for (int y = 0; y < 5; y++) {
        ASSERT_FALSE(f.screen->dirty_rows[y]);
    }
}

TEST(render_paint_only_dirty_rows) {
    RenderFixture f(10, 5);

    /* Initial paint clears everything */
    wp_renderer_paint(&f.renderer);

    /* Mark only row 2 dirty */
    wp_screen_mark_dirty(f.screen, 2);
    ASSERT_TRUE(f.screen->dirty_rows[2]);

    /* Paint again */
    wp_renderer_paint(&f.renderer);

    /* All clean again */
    for (int y = 0; y < 5; y++) {
        ASSERT_FALSE(f.screen->dirty_rows[y]);
    }
}

TEST(render_paint_null_safe) {
    /* Should not crash */
    wp_renderer_paint(NULL);
    ASSERT_TRUE(true);
}

TEST(render_paint_updates_cursor_cache) {
    RenderFixture f(10, 5);

    /* Move cursor */
    wp_screen_set_cursor(f.screen, 3, 2);

    wp_renderer_paint(&f.renderer);

    ASSERT_TRUE(f.renderer.cursor_state_valid);
    ASSERT_EQ(f.renderer.last_cursor_pos.X, (SHORT)3);
    ASSERT_EQ(f.renderer.last_cursor_pos.Y, (SHORT)2);
}

TEST(render_paint_caches_cursor_visibility) {
    RenderFixture f;

    wp_renderer_paint(&f.renderer);
    ASSERT_TRUE(f.renderer.last_cursor_visible);

    /* Hide cursor */
    f.screen->cursor.visible = false;
    wp_screen_mark_dirty(f.screen, 0);  /* Need dirty to trigger paint */
    wp_renderer_paint(&f.renderer);

    ASSERT_FALSE(f.renderer.last_cursor_visible);
}

/*============================================================================
 * Tests - Paint All
 *============================================================================*/

TEST(render_paint_all_forces_repaint) {
    RenderFixture f(10, 5);

    /* Clear dirty flags */
    wp_screen_mark_all_clean(f.screen);
    ASSERT_FALSE(f.screen->full_repaint);

    /* paint_all should mark all dirty then paint */
    wp_renderer_paint_all(&f.renderer);

    /* After paint, everything clean again */
    ASSERT_FALSE(f.screen->full_repaint);
    for (int y = 0; y < 5; y++) {
        ASSERT_FALSE(f.screen->dirty_rows[y]);
    }
}

TEST(render_paint_all_null_safe) {
    wp_renderer_paint_all(NULL);
    ASSERT_TRUE(true);
}

/*============================================================================
 * Tests - Resize
 *============================================================================*/

TEST(render_resize_reallocates) {
    RenderFixture f(10, 5);

    ASSERT_EQ(f.renderer.row_buf_width, 10);

    /* Resize screen */
    wp_screen_resize(f.screen, 20, 10);

    /* Resize renderer */
    bool ok = wp_renderer_resize(&f.renderer);
    ASSERT_TRUE(ok);
    ASSERT_EQ(f.renderer.row_buf_width, 20);
    ASSERT_TRUE(f.renderer.row_buf != NULL);
}

TEST(render_resize_same_width) {
    RenderFixture f(10, 5);

    /* Resize to same width */
    bool ok = wp_renderer_resize(&f.renderer);
    ASSERT_TRUE(ok);
    ASSERT_EQ(f.renderer.row_buf_width, 10);
}

TEST(render_resize_shrink) {
    RenderFixture f(20, 10);

    wp_screen_resize(f.screen, 5, 3);

    bool ok = wp_renderer_resize(&f.renderer);
    ASSERT_TRUE(ok);
    ASSERT_EQ(f.renderer.row_buf_width, 5);
}

/*============================================================================
 * Tests - Set Handle
 *============================================================================*/

TEST(render_set_handle) {
    RenderFixture f;

    /* Change handle */
    HANDLE new_handle = (HANDLE)(intptr_t)0x12345678;
    wp_renderer_set_handle(&f.renderer, new_handle);

    ASSERT_EQ(f.renderer.console_handle, new_handle);
    /* Cursor cache should be invalidated */
    ASSERT_FALSE(f.renderer.cursor_state_valid);
}

TEST(render_set_handle_null_safe) {
    wp_renderer_set_handle(NULL, INVALID_HANDLE_VALUE);
    ASSERT_TRUE(true);
}

/*============================================================================
 * Tests - Alternate Buffer Rendering
 *============================================================================*/

TEST(render_paints_alternate_buffer) {
    RenderFixture f(10, 5);

    /* Write to main */
    wp_screen_put_char(f.screen, 'M');

    /* Enter alternate */
    wp_screen_enter_alternate(f.screen);
    WPScreenBuffer* alt = wp_screen_active(f.screen);
    wp_screen_put_char(alt, 'A');

    /* Paint should read from alternate (clean alternate's dirty flags) */
    wp_renderer_paint(&f.renderer);

    ASSERT_FALSE(alt->full_repaint);
    ASSERT_FALSE(alt->dirty_rows[0]);

    /* Cursor cache should reflect alternate's cursor */
    ASSERT_EQ(f.renderer.last_cursor_pos.X, (SHORT)alt->cursor.x);
    ASSERT_EQ(f.renderer.last_cursor_pos.Y, (SHORT)alt->cursor.y);

    wp_screen_leave_alternate(f.screen);
}

TEST(render_switches_to_main_after_leave) {
    RenderFixture f(10, 5);

    wp_screen_enter_alternate(f.screen);
    WPScreenBuffer* alt = wp_screen_active(f.screen);
    wp_screen_put_char(alt, 'A');
    wp_renderer_paint(&f.renderer);

    wp_screen_leave_alternate(f.screen);

    /* After leave, main is dirty (full_repaint) */
    ASSERT_TRUE(f.screen->full_repaint);

    /* Paint should read from main now */
    wp_renderer_paint(&f.renderer);
    ASSERT_FALSE(f.screen->full_repaint);
}

/*============================================================================
 * Tests - Content Conversion
 *============================================================================*/

TEST(render_paint_with_content) {
    /* Verify paint doesn't crash with various cell contents */
    RenderFixture f(10, 3);

    /* ASCII */
    wp_screen_put_char(f.screen, 'H');
    wp_screen_put_char(f.screen, 'i');

    /* BMP character */
    wp_screen_put_char(f.screen, 0x20AC);  /* Euro sign */

    /* Supplementary plane (should be handled as U+FFFD internally) */
    wp_screen_put_char(f.screen, 0x1F600);  /* Emoji */

    /* Zero codepoint cell */
    WPScreenCell* cell = wp_screen_cell_at(f.screen, 5, 0);
    cell->codepoint = 0;

    /* Wide trail cell */
    cell = wp_screen_cell_at(f.screen, 6, 0);
    cell->wide_trail = true;

    /* Paint should handle all of these without crashing */
    wp_renderer_paint(&f.renderer);
    ASSERT_TRUE(true);
}

TEST(render_paint_full_screen) {
    /* Fill entire screen and paint */
    RenderFixture f(80, 24);

    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            WPScreenCell* cell = wp_screen_cell_at(f.screen, x, y);
            cell->codepoint = 'A' + (x % 26);
            cell->attributes = DEFAULT_ATTRS;
        }
    }

    wp_screen_mark_all_dirty(f.screen);
    wp_renderer_paint(&f.renderer);

    /* All clean after paint */
    ASSERT_FALSE(f.screen->full_repaint);
    for (int y = 0; y < 24; y++) {
        ASSERT_FALSE(f.screen->dirty_rows[y]);
    }
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
