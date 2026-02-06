/**
 * @file test_screen.cpp
 * @brief Unit tests for the screen buffer
 */

#include "test_harness.h"
#include "../src/winpatina_screen.h"

/*============================================================================
 * Test Helpers
 *============================================================================*/

/** Default attributes used throughout tests */
#define DEFAULT_ATTRS (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

/** Custom attributes for testing attribute changes */
#define CUSTOM_ATTRS (FOREGROUND_RED | FOREGROUND_INTENSITY)

/** Create a small screen for testing. Caller must wp_screen_destroy(). */
static WPScreenBuffer* make_screen(int w, int h)
{
    return wp_screen_create(w, h, DEFAULT_ATTRS);
}

/** Get the codepoint at (x, y) */
static uint32_t cp_at(WPScreenBuffer* s, int x, int y)
{
    WPScreenCell* cell = wp_screen_cell_at(s, x, y);
    return cell ? cell->codepoint : 0;
}

/** Get the attributes at (x, y) */
static WORD attr_at(WPScreenBuffer* s, int x, int y)
{
    WPScreenCell* cell = wp_screen_cell_at(s, x, y);
    return cell ? cell->attributes : 0;
}

/*============================================================================
 * Tests - Lifecycle
 *============================================================================*/

TEST(create_destroy) {
    WPScreenBuffer* s = make_screen(80, 25);
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ(s->width, 80);
    ASSERT_EQ(s->height, 25);
    wp_screen_destroy(s);
}

TEST(create_invalid_dimensions) {
    ASSERT_TRUE(wp_screen_create(0, 25, DEFAULT_ATTRS) == NULL);
    ASSERT_TRUE(wp_screen_create(80, 0, DEFAULT_ATTRS) == NULL);
    ASSERT_TRUE(wp_screen_create(-1, 25, DEFAULT_ATTRS) == NULL);
}

TEST(destroy_null_safe) {
    /* Should not crash */
    wp_screen_destroy(NULL);
    ASSERT_TRUE(true);
}

TEST(initial_state) {
    WPScreenBuffer* s = make_screen(10, 5);

    /* All cells should be spaces with default attrs */
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 10; x++) {
            ASSERT_EQ(cp_at(s, x, y), (uint32_t)0x20);
            ASSERT_EQ(attr_at(s, x, y), (WORD)DEFAULT_ATTRS);
        }
    }

    /* Cursor at (0,0), visible */
    ASSERT_EQ(s->cursor.x, 0);
    ASSERT_EQ(s->cursor.y, 0);
    ASSERT_TRUE(s->cursor.visible);
    ASSERT_FALSE(s->cursor.pending_wrap);

    /* Scroll region covers full screen */
    ASSERT_EQ(s->scroll_top, 0);
    ASSERT_EQ(s->scroll_bottom, 4);

    /* Full repaint flagged */
    ASSERT_TRUE(s->full_repaint);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Cell Access
 *============================================================================*/

TEST(cell_at_valid) {
    WPScreenBuffer* s = make_screen(10, 5);
    ASSERT_TRUE(wp_screen_cell_at(s, 0, 0) != NULL);
    ASSERT_TRUE(wp_screen_cell_at(s, 9, 4) != NULL);
    wp_screen_destroy(s);
}

TEST(cell_at_out_of_bounds) {
    WPScreenBuffer* s = make_screen(10, 5);
    ASSERT_TRUE(wp_screen_cell_at(s, -1, 0) == NULL);
    ASSERT_TRUE(wp_screen_cell_at(s, 10, 0) == NULL);
    ASSERT_TRUE(wp_screen_cell_at(s, 0, -1) == NULL);
    ASSERT_TRUE(wp_screen_cell_at(s, 0, 5) == NULL);
    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - put_char
 *============================================================================*/

TEST(put_char_basic) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_put_char(s, 'A');

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');
    ASSERT_EQ(s->cursor.x, 1);
    ASSERT_EQ(s->cursor.y, 0);
    ASSERT_TRUE(s->dirty_rows[0]);

    wp_screen_destroy(s);
}

TEST(put_char_uses_current_attrs) {
    WPScreenBuffer* s = make_screen(10, 5);
    s->current_attrs = CUSTOM_ATTRS;

    wp_screen_put_char(s, 'X');

    ASSERT_EQ(attr_at(s, 0, 0), (WORD)CUSTOM_ATTRS);
    wp_screen_destroy(s);
}

TEST(put_char_advances_cursor) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_put_char(s, 'A');
    wp_screen_put_char(s, 'B');
    wp_screen_put_char(s, 'C');

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)'B');
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)'C');
    ASSERT_EQ(s->cursor.x, 3);

    wp_screen_destroy(s);
}

TEST(put_char_pending_wrap) {
    /* On a 5-wide screen, writing 5 chars should leave cursor at col 4
     * with pending_wrap set, NOT wrapped to next line yet. */
    WPScreenBuffer* s = make_screen(5, 3);

    for (int i = 0; i < 5; i++) {
        wp_screen_put_char(s, 'A' + i);
    }

    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)'E');
    ASSERT_EQ(s->cursor.x, 4);
    ASSERT_EQ(s->cursor.y, 0);
    ASSERT_TRUE(s->cursor.pending_wrap);

    /* Next character triggers the wrap */
    wp_screen_put_char(s, 'F');

    ASSERT_EQ(s->cursor.x, 1);
    ASSERT_EQ(s->cursor.y, 1);
    ASSERT_FALSE(s->cursor.pending_wrap);
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'F');

    wp_screen_destroy(s);
}

TEST(put_char_wrap_scrolls_at_bottom) {
    /* 5 wide, 2 tall. Fill both lines then write one more -> scroll. */
    WPScreenBuffer* s = make_screen(5, 2);

    /* Fill line 0 */
    for (int i = 0; i < 5; i++) wp_screen_put_char(s, 'A');
    /* Fill line 1 */
    for (int i = 0; i < 5; i++) wp_screen_put_char(s, 'B');

    /* Now pending_wrap is set on line 1. Next char should scroll. */
    wp_screen_put_char(s, 'C');

    /* Line 0 should now contain what was line 1 (all 'B') */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'B');
    /* Line 1 should have 'C' at col 0, rest blank */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'C');
    ASSERT_EQ(cp_at(s, 1, 1), (uint32_t)0x20);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Erase Display
 *============================================================================*/

TEST(erase_display_mode0) {
    /* Mode 0: cursor to end */
    WPScreenBuffer* s = make_screen(5, 3);

    /* Fill screen with 'X' */
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'X';

    wp_screen_set_cursor(s, 2, 1);
    wp_screen_erase_display(s, 0);

    /* Before cursor: unchanged */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'X');
    ASSERT_EQ(cp_at(s, 1, 1), (uint32_t)'X');

    /* At and after cursor on line 1: erased */
    ASSERT_EQ(cp_at(s, 2, 1), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 4, 1), (uint32_t)0x20);

    /* All of line 2: erased */
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)0x20);

    wp_screen_destroy(s);
}

TEST(erase_display_mode1) {
    /* Mode 1: start to cursor */
    WPScreenBuffer* s = make_screen(5, 3);

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'X';

    wp_screen_set_cursor(s, 2, 1);
    wp_screen_erase_display(s, 1);

    /* All of line 0: erased */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)0x20);

    /* Start to cursor on line 1: erased (inclusive) */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 2, 1), (uint32_t)0x20);

    /* After cursor on line 1: unchanged */
    ASSERT_EQ(cp_at(s, 3, 1), (uint32_t)'X');

    /* Line 2: unchanged */
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'X');

    wp_screen_destroy(s);
}

TEST(erase_display_mode2) {
    /* Mode 2: entire screen */
    WPScreenBuffer* s = make_screen(5, 3);

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'X';

    wp_screen_set_cursor(s, 2, 1);
    wp_screen_erase_display(s, 2);

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            ASSERT_EQ(cp_at(s, x, y), (uint32_t)0x20);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Erase Line
 *============================================================================*/

TEST(erase_line_mode0) {
    /* Mode 0: cursor to end of line */
    WPScreenBuffer* s = make_screen(5, 2);

    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'X';

    wp_screen_set_cursor(s, 2, 0);
    wp_screen_erase_line(s, 0);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'X');
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)'X');
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 3, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)0x20);

    wp_screen_destroy(s);
}

TEST(erase_line_mode1) {
    /* Mode 1: start to cursor (inclusive) */
    WPScreenBuffer* s = make_screen(5, 2);

    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'X';

    wp_screen_set_cursor(s, 2, 0);
    wp_screen_erase_line(s, 1);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 3, 0), (uint32_t)'X');
    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)'X');

    wp_screen_destroy(s);
}

TEST(erase_line_mode2) {
    /* Mode 2: entire line */
    WPScreenBuffer* s = make_screen(5, 2);

    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'X';

    /* Line 1 has different content to confirm only line 0 is erased */
    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 1)->codepoint = 'Y';

    wp_screen_set_cursor(s, 2, 0);
    wp_screen_erase_line(s, 2);

    for (int x = 0; x < 5; x++)
        ASSERT_EQ(cp_at(s, x, 0), (uint32_t)0x20);

    /* Line 1 untouched */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'Y');

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Cursor Movement
 *============================================================================*/

TEST(set_cursor_basic) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_set_cursor(s, 3, 2);

    ASSERT_EQ(s->cursor.x, 3);
    ASSERT_EQ(s->cursor.y, 2);
    ASSERT_FALSE(s->cursor.pending_wrap);

    wp_screen_destroy(s);
}

TEST(set_cursor_clamps) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_set_cursor(s, 100, 200);
    ASSERT_EQ(s->cursor.x, 9);
    ASSERT_EQ(s->cursor.y, 4);

    wp_screen_set_cursor(s, -5, -10);
    ASSERT_EQ(s->cursor.x, 0);
    ASSERT_EQ(s->cursor.y, 0);

    wp_screen_destroy(s);
}

TEST(set_cursor_clears_pending_wrap) {
    WPScreenBuffer* s = make_screen(5, 2);

    /* Force pending wrap */
    for (int i = 0; i < 5; i++) wp_screen_put_char(s, 'A');
    ASSERT_TRUE(s->cursor.pending_wrap);

    wp_screen_set_cursor(s, 0, 0);
    ASSERT_FALSE(s->cursor.pending_wrap);

    wp_screen_destroy(s);
}

TEST(move_cursor_relative) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_set_cursor(s, 5, 2);
    wp_screen_move_cursor(s, -2, 1);

    ASSERT_EQ(s->cursor.x, 3);
    ASSERT_EQ(s->cursor.y, 3);

    wp_screen_destroy(s);
}

TEST(move_cursor_clamps) {
    WPScreenBuffer* s = make_screen(10, 5);

    wp_screen_set_cursor(s, 0, 0);
    wp_screen_move_cursor(s, -5, -5);

    ASSERT_EQ(s->cursor.x, 0);
    ASSERT_EQ(s->cursor.y, 0);

    wp_screen_destroy(s);
}

TEST(save_restore_cursor) {
    WPScreenBuffer* s = make_screen(10, 5);
    s->current_attrs = CUSTOM_ATTRS;

    wp_screen_set_cursor(s, 3, 2);
    wp_screen_save_cursor(s);

    wp_screen_set_cursor(s, 7, 4);
    s->current_attrs = DEFAULT_ATTRS;

    wp_screen_restore_cursor(s);

    ASSERT_EQ(s->cursor.x, 3);
    ASSERT_EQ(s->cursor.y, 2);
    ASSERT_EQ(s->current_attrs, (WORD)CUSTOM_ATTRS);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Scrolling
 *============================================================================*/

TEST(scroll_up_basic) {
    WPScreenBuffer* s = make_screen(3, 4);

    /* Label each row: row 0 = 'A', row 1 = 'B', etc. */
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'A' + y;

    wp_screen_scroll(s, 1);

    /* Row 0 should now be what was row 1 */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'B');
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'C');
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'D');
    /* Row 3 should be blank */
    ASSERT_EQ(cp_at(s, 0, 3), (uint32_t)0x20);

    wp_screen_destroy(s);
}

TEST(scroll_down_basic) {
    WPScreenBuffer* s = make_screen(3, 4);

    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'A' + y;

    wp_screen_scroll(s, -1);

    /* Row 0 should be blank */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)0x20);
    /* Row 1 should be what was row 0 */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'A');
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'B');
    ASSERT_EQ(cp_at(s, 0, 3), (uint32_t)'C');

    wp_screen_destroy(s);
}

TEST(scroll_respects_region) {
    WPScreenBuffer* s = make_screen(3, 5);

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'A' + y;

    /* Set scroll region to rows 1-3 */
    wp_screen_set_scroll_region(s, 1, 3);
    wp_screen_scroll(s, 1);

    /* Row 0: untouched */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');
    /* Row 1: was row 2 */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'C');
    /* Row 2: was row 3 */
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'D');
    /* Row 3: blank (scrolled in) */
    ASSERT_EQ(cp_at(s, 0, 3), (uint32_t)0x20);
    /* Row 4: untouched */
    ASSERT_EQ(cp_at(s, 0, 4), (uint32_t)'E');

    wp_screen_destroy(s);
}

TEST(scroll_up_by_more_than_region) {
    /* Scrolling by more than the region height clears the region */
    WPScreenBuffer* s = make_screen(3, 3);

    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'X';

    wp_screen_scroll(s, 100);

    for (int y = 0; y < 3; y++)
        ASSERT_EQ(cp_at(s, 0, y), (uint32_t)0x20);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Insert/Delete Lines
 *============================================================================*/

TEST(insert_lines) {
    WPScreenBuffer* s = make_screen(3, 5);

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'A' + y;

    /* Cursor on row 1, insert 1 line */
    wp_screen_set_cursor(s, 0, 1);
    wp_screen_insert_lines(s, 1);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');  /* Unchanged */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)0x20); /* Inserted blank */
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'B');  /* Shifted down */
    ASSERT_EQ(cp_at(s, 0, 3), (uint32_t)'C');  /* Shifted down */
    ASSERT_EQ(cp_at(s, 0, 4), (uint32_t)'D');  /* 'E' was pushed off */

    wp_screen_destroy(s);
}

TEST(delete_lines) {
    WPScreenBuffer* s = make_screen(3, 5);

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 3; x++)
            wp_screen_cell_at(s, x, y)->codepoint = 'A' + y;

    /* Cursor on row 1, delete 1 line */
    wp_screen_set_cursor(s, 0, 1);
    wp_screen_delete_lines(s, 1);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');  /* Unchanged */
    ASSERT_EQ(cp_at(s, 0, 1), (uint32_t)'C');  /* Was row 2 */
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)'D');  /* Was row 3 */
    ASSERT_EQ(cp_at(s, 0, 3), (uint32_t)'E');  /* Was row 4 */
    ASSERT_EQ(cp_at(s, 0, 4), (uint32_t)0x20); /* Blank scrolled in */

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Insert/Delete Characters
 *============================================================================*/

TEST(insert_chars) {
    WPScreenBuffer* s = make_screen(5, 2);

    /* Fill row 0: A B C D E */
    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'A' + x;

    /* Cursor at col 1, insert 2 chars */
    wp_screen_set_cursor(s, 1, 0);
    wp_screen_insert_chars(s, 2);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');  /* Unchanged */
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)0x20); /* Inserted */
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)0x20); /* Inserted */
    ASSERT_EQ(cp_at(s, 3, 0), (uint32_t)'B');  /* Shifted right */
    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)'C');  /* Shifted right; D,E lost */

    wp_screen_destroy(s);
}

TEST(delete_chars) {
    WPScreenBuffer* s = make_screen(5, 2);

    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'A' + x;

    /* Cursor at col 1, delete 2 chars */
    wp_screen_set_cursor(s, 1, 0);
    wp_screen_delete_chars(s, 2);

    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');  /* Unchanged */
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)'D');  /* Shifted left */
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)'E');  /* Shifted left */
    ASSERT_EQ(cp_at(s, 3, 0), (uint32_t)0x20); /* Blank */
    ASSERT_EQ(cp_at(s, 4, 0), (uint32_t)0x20); /* Blank */

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Dirty Tracking
 *============================================================================*/

TEST(dirty_tracking_basic) {
    WPScreenBuffer* s = make_screen(5, 3);

    wp_screen_mark_all_clean(s);

    ASSERT_FALSE(s->dirty_rows[0]);
    ASSERT_FALSE(s->dirty_rows[1]);
    ASSERT_FALSE(s->dirty_rows[2]);
    ASSERT_FALSE(s->full_repaint);

    wp_screen_mark_dirty(s, 1);
    ASSERT_FALSE(s->dirty_rows[0]);
    ASSERT_TRUE(s->dirty_rows[1]);
    ASSERT_FALSE(s->dirty_rows[2]);

    wp_screen_mark_all_dirty(s);
    ASSERT_TRUE(s->dirty_rows[0]);
    ASSERT_TRUE(s->dirty_rows[1]);
    ASSERT_TRUE(s->dirty_rows[2]);
    ASSERT_TRUE(s->full_repaint);

    wp_screen_destroy(s);
}

TEST(put_char_marks_row_dirty) {
    WPScreenBuffer* s = make_screen(5, 3);
    wp_screen_mark_all_clean(s);

    wp_screen_set_cursor(s, 0, 1);
    wp_screen_put_char(s, 'X');

    ASSERT_FALSE(s->dirty_rows[0]);
    ASSERT_TRUE(s->dirty_rows[1]);
    ASSERT_FALSE(s->dirty_rows[2]);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Scroll Region
 *============================================================================*/

TEST(set_scroll_region_basic) {
    WPScreenBuffer* s = make_screen(10, 10);

    wp_screen_set_scroll_region(s, 2, 7);

    ASSERT_EQ(s->scroll_top, 2);
    ASSERT_EQ(s->scroll_bottom, 7);
    /* Cursor should move to home */
    ASSERT_EQ(s->cursor.x, 0);
    ASSERT_EQ(s->cursor.y, 0);

    wp_screen_destroy(s);
}

TEST(set_scroll_region_invalid) {
    /* top >= bottom should be rejected */
    WPScreenBuffer* s = make_screen(10, 10);

    wp_screen_set_scroll_region(s, 5, 5);
    /* Should remain at defaults */
    ASSERT_EQ(s->scroll_top, 0);
    ASSERT_EQ(s->scroll_bottom, 9);

    wp_screen_set_scroll_region(s, 7, 3);
    ASSERT_EQ(s->scroll_top, 0);
    ASSERT_EQ(s->scroll_bottom, 9);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Alternate Buffer
 *============================================================================*/

TEST(alternate_buffer_enter_leave) {
    WPScreenBuffer* s = make_screen(5, 3);

    /* Write something to main buffer */
    wp_screen_put_char(s, 'M');
    wp_screen_set_cursor(s, 2, 1);

    wp_screen_enter_alternate(s);

    ASSERT_TRUE(s->using_alternate);
    ASSERT_TRUE(s->alternate != NULL);

    /* Alternate buffer should be clean */
    ASSERT_EQ(cp_at(s->alternate, 0, 0), (uint32_t)0x20);

    /* Write to alternate */
    wp_screen_put_char(s->alternate, 'A');

    wp_screen_leave_alternate(s);

    ASSERT_FALSE(s->using_alternate);
    /* Main buffer content should be intact */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'M');
    /* Cursor should be restored */
    ASSERT_EQ(s->cursor.x, 2);
    ASSERT_EQ(s->cursor.y, 1);

    wp_screen_destroy(s);
}

TEST(alternate_buffer_double_enter) {
    /* Entering alternate twice should be a no-op */
    WPScreenBuffer* s = make_screen(5, 3);

    wp_screen_enter_alternate(s);
    wp_screen_enter_alternate(s);

    ASSERT_TRUE(s->using_alternate);

    wp_screen_leave_alternate(s);
    ASSERT_FALSE(s->using_alternate);

    wp_screen_destroy(s);
}

TEST(alternate_buffer_leave_without_enter) {
    /* Leaving without entering should be a no-op */
    WPScreenBuffer* s = make_screen(5, 3);

    wp_screen_leave_alternate(s);
    ASSERT_FALSE(s->using_alternate);

    wp_screen_destroy(s);
}

/*============================================================================
 * Tests - Resize
 *============================================================================*/

TEST(resize_grow) {
    WPScreenBuffer* s = make_screen(3, 2);

    wp_screen_put_char(s, 'A');
    wp_screen_put_char(s, 'B');

    bool ok = wp_screen_resize(s, 5, 4);
    ASSERT_TRUE(ok);
    ASSERT_EQ(s->width, 5);
    ASSERT_EQ(s->height, 4);

    /* Old content preserved */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)'B');

    /* New cells are blank */
    ASSERT_EQ(cp_at(s, 3, 0), (uint32_t)0x20);
    ASSERT_EQ(cp_at(s, 0, 2), (uint32_t)0x20);

    /* Scroll region reset to full screen */
    ASSERT_EQ(s->scroll_top, 0);
    ASSERT_EQ(s->scroll_bottom, 3);

    ASSERT_TRUE(s->full_repaint);

    wp_screen_destroy(s);
}

TEST(resize_shrink) {
    WPScreenBuffer* s = make_screen(5, 4);

    /* Put content that will be partially clipped */
    for (int x = 0; x < 5; x++)
        wp_screen_cell_at(s, x, 0)->codepoint = 'A' + x;

    wp_screen_set_cursor(s, 4, 3);

    bool ok = wp_screen_resize(s, 3, 2);
    ASSERT_TRUE(ok);
    ASSERT_EQ(s->width, 3);
    ASSERT_EQ(s->height, 2);

    /* Content within new bounds preserved */
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'A');
    ASSERT_EQ(cp_at(s, 1, 0), (uint32_t)'B');
    ASSERT_EQ(cp_at(s, 2, 0), (uint32_t)'C');

    /* Cursor clamped */
    ASSERT_EQ(s->cursor.x, 2);
    ASSERT_EQ(s->cursor.y, 1);

    wp_screen_destroy(s);
}

TEST(resize_same_size) {
    WPScreenBuffer* s = make_screen(5, 3);
    wp_screen_put_char(s, 'Z');

    bool ok = wp_screen_resize(s, 5, 3);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cp_at(s, 0, 0), (uint32_t)'Z');

    wp_screen_destroy(s);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
