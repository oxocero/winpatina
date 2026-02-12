/**
 * @file test_dispatch.cpp
 * @brief Unit tests for the VT dispatch handler
 *
 * Tests the full pipeline: VT byte stream -> parser -> dispatch -> screen buffer.
 * Each test feeds VT sequences and verifies the resulting screen state.
 */

#include "test_harness.h"
#include "../src/winpatina_dispatch.h"

#include <string.h>
#include <string>
#include <vector>
#include <wchar.h>

/*============================================================================
 * Test Helpers
 *============================================================================*/

static const WORD DEFAULT_ATTRS = 0x07;  /* White on black */

struct TestFixture {
    WPScreenBuffer* screen;
    WPVTParser parser;
    WPDispatchState dispatch;

    TestFixture(int width = 80, int height = 24) {
        screen = wp_screen_create(width, height, DEFAULT_ATTRS);
        wp_dispatch_init(&dispatch, screen, DEFAULT_ATTRS, true,
                         NULL, INVALID_HANDLE_VALUE);
        wp_vt_parser_init(&parser, NULL);
        wp_dispatch_attach(&dispatch, &parser);
    }

    ~TestFixture() {
        wp_screen_destroy(screen);
    }

    void feed(const char* str) {
        wp_vt_parser_feed(&parser, (const uint8_t*)str, strlen(str));
    }

    void feed_bytes(const uint8_t* data, size_t len) {
        wp_vt_parser_feed(&parser, data, len);
    }

    WPScreenCell* cell(int x, int y) {
        return wp_screen_cell_at(screen, x, y);
    }
};

/** Write-back recorder for DSR tests */
struct WriteBackRecorder {
    std::vector<uint8_t> data;

    static void callback(void* ud, const uint8_t* data, size_t len) {
        auto* rec = (WriteBackRecorder*)ud;
        rec->data.insert(rec->data.end(), data, data + len);
    }

    std::string as_string() const {
        return std::string(data.begin(), data.end());
    }
};

/*============================================================================
 * Tests - Print Characters
 *============================================================================*/

TEST(dispatch_print_ascii) {
    TestFixture f;
    f.feed("Hello");

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'H');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'e');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)'l');
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)'l');
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'o');
    ASSERT_EQ(f.screen->cursor.x, 5);
    ASSERT_EQ(f.screen->cursor.y, 0);
}

TEST(dispatch_print_utf8) {
    TestFixture f;
    /* Euro sign: U+20AC = 0xE2 0x82 0xAC */
    uint8_t data[] = { 0xE2, 0x82, 0xAC };
    f.feed_bytes(data, sizeof(data));

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)0x20AC);
    ASSERT_EQ(f.screen->cursor.x, 1);
}

/*============================================================================
 * Tests - C0 Controls
 *============================================================================*/

TEST(dispatch_cr) {
    TestFixture f;
    f.feed("Hello\rWorld");

    /* After CR, cursor returns to column 0, "World" overwrites "Hello" */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'W');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'o');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)'r');
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)'l');
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'d');
}

TEST(dispatch_lf) {
    TestFixture f;
    f.feed("A");
    f.feed("\n");

    ASSERT_EQ(f.screen->cursor.y, 1);
    /* LF does NOT reset x to 0 */
    ASSERT_EQ(f.screen->cursor.x, 1);
}

TEST(dispatch_crlf) {
    TestFixture f;
    f.feed("Line1\r\nLine2");

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'L');
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'1');
    ASSERT_EQ(f.cell(0, 1)->codepoint, (uint32_t)'L');
    ASSERT_EQ(f.cell(4, 1)->codepoint, (uint32_t)'2');
}

TEST(dispatch_backspace) {
    TestFixture f;
    f.feed("AB\x08X");

    /* BS moves cursor back, 'X' overwrites 'B' */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'X');
}

TEST(dispatch_backspace_at_left_edge) {
    TestFixture f;
    f.feed("\x08");

    /* BS at column 0 should be a no-op */
    ASSERT_EQ(f.screen->cursor.x, 0);
}

TEST(dispatch_tab) {
    TestFixture f;
    f.feed("A\tB");

    /* Tab should advance to next 8-column stop */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.cell(8, 0)->codepoint, (uint32_t)'B');
}

TEST(dispatch_tab_aligned) {
    TestFixture f;
    /* Write 8 chars then tab: cursor at col 8, tab to col 16 */
    f.feed("12345678\tX");
    ASSERT_EQ(f.cell(16, 0)->codepoint, (uint32_t)'X');
}

TEST(dispatch_vt_ff_as_lf) {
    TestFixture f;
    /* VT (0x0B) and FF (0x0C) should act like LF */
    uint8_t data[] = { 'A', 0x0B, 'B', 0x0C, 'C' };
    f.feed_bytes(data, sizeof(data));

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.screen->cursor.y, 2);
}

/*============================================================================
 * Tests - Cursor Movement (CSI)
 *============================================================================*/

TEST(dispatch_cup_basic) {
    TestFixture f;
    /* CSI 5;10 H -> cursor to row 5, col 10 (1-based) */
    f.feed("\x1b[5;10H");

    ASSERT_EQ(f.screen->cursor.x, 9);
    ASSERT_EQ(f.screen->cursor.y, 4);
}

TEST(dispatch_cup_default) {
    TestFixture f;
    f.feed("Hello");
    /* CSI H with no params -> home (1,1) = (0,0) */
    f.feed("\x1b[H");

    ASSERT_EQ(f.screen->cursor.x, 0);
    ASSERT_EQ(f.screen->cursor.y, 0);
}

TEST(dispatch_cup_hvp) {
    /* CSI f is the same as CSI H */
    TestFixture f;
    f.feed("\x1b[3;7f");

    ASSERT_EQ(f.screen->cursor.x, 6);
    ASSERT_EQ(f.screen->cursor.y, 2);
}

TEST(dispatch_cuu) {
    TestFixture f;
    f.feed("\x1b[10;1H");  /* Go to row 10 */
    f.feed("\x1b[3A");     /* CUU: up 3 */

    ASSERT_EQ(f.screen->cursor.y, 6);
}

TEST(dispatch_cud) {
    TestFixture f;
    f.feed("\x1b[2B");  /* CUD: down 2 */

    ASSERT_EQ(f.screen->cursor.y, 2);
}

TEST(dispatch_cuf) {
    TestFixture f;
    f.feed("\x1b[5C");  /* CUF: right 5 */

    ASSERT_EQ(f.screen->cursor.x, 5);
}

TEST(dispatch_cub) {
    TestFixture f;
    f.feed("\x1b[10;10H");  /* Position at col 10 */
    f.feed("\x1b[3D");      /* CUB: left 3 */

    ASSERT_EQ(f.screen->cursor.x, 6);
}

TEST(dispatch_cnl) {
    /* CNL: move down N lines AND set column to 0 */
    TestFixture f;
    f.feed("\x1b[5;10H");  /* Position at row 5, col 10 */
    f.feed("\x1b[2E");     /* CNL: down 2 */

    ASSERT_EQ(f.screen->cursor.y, 6);
    ASSERT_EQ(f.screen->cursor.x, 0);
}

TEST(dispatch_cpl) {
    /* CPL: move up N lines AND set column to 0 */
    TestFixture f;
    f.feed("\x1b[5;10H");  /* Position at row 5, col 10 */
    f.feed("\x1b[2F");     /* CPL: up 2 */

    ASSERT_EQ(f.screen->cursor.y, 2);
    ASSERT_EQ(f.screen->cursor.x, 0);
}

TEST(dispatch_cha) {
    /* CHA: set column to N (1-based) */
    TestFixture f;
    f.feed("\x1b[15G");

    ASSERT_EQ(f.screen->cursor.x, 14);
}

TEST(dispatch_vpa) {
    /* VPA: set row to N (1-based), keep column */
    TestFixture f;
    f.feed("\x1b[10;5H");  /* Row 10, col 5 */
    f.feed("\x1b[3d");     /* VPA: row 3 */

    ASSERT_EQ(f.screen->cursor.y, 2);
    ASSERT_EQ(f.screen->cursor.x, 4);  /* Column unchanged */
}

/*============================================================================
 * Tests - Erase Operations
 *============================================================================*/

TEST(dispatch_erase_display_full) {
    TestFixture f;
    f.feed("Hello");
    f.feed("\x1b[2J");  /* ED 2: erase entire display */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)' ');
}

TEST(dispatch_erase_line_to_end) {
    TestFixture f;
    f.feed("Hello World");
    f.feed("\x1b[6G");   /* Move to column 6 (0-based: 5) */
    f.feed("\x1b[K");    /* EL 0: erase from cursor to end of line */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'H');
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'o');
    ASSERT_EQ(f.cell(5, 0)->codepoint, (uint32_t)' ');
    ASSERT_EQ(f.cell(6, 0)->codepoint, (uint32_t)' ');
}

TEST(dispatch_erase_chars) {
    /* ECH: erase N characters at cursor without moving cursor */
    TestFixture f;
    f.feed("ABCDEF");
    f.feed("\x1b[3G");    /* Column 3 (0-based: 2) */
    f.feed("\x1b[2X");    /* ECH 2: erase 2 chars */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'B');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)' ');  /* Erased */
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)' ');  /* Erased */
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'E');
    ASSERT_EQ(f.cell(5, 0)->codepoint, (uint32_t)'F');
    /* Cursor should not have moved */
    ASSERT_EQ(f.screen->cursor.x, 2);
}

/*============================================================================
 * Tests - SGR (Colour and Attributes)
 *============================================================================*/

TEST(dispatch_sgr_reset) {
    TestFixture f;
    f.feed("\x1b[31m");  /* Set red foreground */
    f.feed("\x1b[0m");   /* Reset */
    f.feed("A");

    /* After reset, attributes should be default */
    ASSERT_EQ(f.cell(0, 0)->attributes, DEFAULT_ATTRS);
}

TEST(dispatch_sgr_fg_colour) {
    TestFixture f;
    f.feed("\x1b[31m");  /* Red foreground (ANSI 1) */
    f.feed("R");

    /* Red fg = Win32 0x04 */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0x0F, (WORD)0x04);
}

TEST(dispatch_sgr_bg_colour) {
    TestFixture f;
    f.feed("\x1b[44m");  /* Blue background (ANSI 4) */
    f.feed("B");

    /* Blue bg = Win32 0x10 */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0xF0, (WORD)0x10);
}

TEST(dispatch_sgr_bold) {
    TestFixture f;
    f.feed("\x1b[1m");  /* Bold */
    f.feed("B");

    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_TRUE((attrs & FOREGROUND_INTENSITY) != 0);
}

TEST(dispatch_sgr_dim) {
    TestFixture f;
    f.feed("\x1b[2m");  /* Dim/faint */
    f.feed("d");

    WORD attrs = f.cell(0, 0)->attributes;
    /* Default white should dim to grey in translation mode. */
    ASSERT_EQ(attrs & 0x0F, (WORD)0x08);
}

TEST(dispatch_sgr_dim_then_bold) {
    TestFixture f;
    f.feed("\x1b[2m");  /* Dim */
    f.feed("\x1b[1m");  /* Bold should cancel dim */
    f.feed("B");

    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_TRUE((attrs & FOREGROUND_INTENSITY) != 0);
}

TEST(dispatch_sgr_underline) {
    TestFixture f;
    f.feed("\x1b[4m");  /* Underline */
    f.feed("U");

    WORD attrs = f.cell(0, 0)->attributes;
    /* LVB underline (0x8000) should be set (fixture has has_lvb=true) */
    ASSERT_TRUE((attrs & 0x8000) != 0);
}

TEST(dispatch_sgr_italic_fallback) {
    TestFixture f;
    f.feed("\x1b[3m");  /* Italic */
    f.feed("I");

    WORD attrs = f.cell(0, 0)->attributes;
    /* Italic falls back to LVB underline on Win32. */
    ASSERT_TRUE((attrs & 0x8000) != 0);
}

TEST(dispatch_sgr_reverse) {
    TestFixture f;
    f.feed("\x1b[31;44m");  /* Red fg, blue bg */
    f.feed("\x1b[7m");      /* Reverse */
    f.feed("X");

    WORD attrs = f.cell(0, 0)->attributes;
    /* Reversed: fg should be blue (0x01), bg should be red (0x40) */
    ASSERT_EQ(attrs & 0x07, (WORD)0x01);
    ASSERT_EQ(attrs & 0x70, (WORD)0x40);
}

TEST(dispatch_sgr_bright_fg) {
    TestFixture f;
    f.feed("\x1b[91m");  /* Bright red foreground */
    f.feed("R");

    /* Bright red = ANSI 9 = Win32 0x0C (INTENSITY | RED) */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0x0F, (WORD)0x0C);
}

TEST(dispatch_sgr_bright_bg) {
    TestFixture f;
    f.feed("\x1b[104m");  /* Bright blue background */
    f.feed("B");

    /* Bright blue bg = ANSI 12 = Win32 0x90 */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0xF0, (WORD)0x90);
}

TEST(dispatch_sgr_256_fg) {
    TestFixture f;
    /* 38;5;196 = 256-colour foreground, index 196 = bright red */
    f.feed("\x1b[38;5;196m");
    f.feed("X");

    /* Index 196 quantises to ANSI bright red (9) = Win32 0x0C */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0x0F, (WORD)0x0C);
}

TEST(dispatch_sgr_256_bg) {
    TestFixture f;
    /* 48;5;21 = 256-colour background, index 21 = blue in cube */
    f.feed("\x1b[48;5;21m");
    f.feed("X");

    /* Index 21 = cube (0,0,5) = pure blue, should quantise to bright blue (12)
     * Win32 bg for 12 = 0x90 */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0xF0, (WORD)0x90);
}

TEST(dispatch_sgr_rgb_fg) {
    TestFixture f;
    /* 38;2;255;0;0 = RGB foreground, pure red */
    f.feed("\x1b[38;2;255;0;0m");
    f.feed("X");

    /* Pure red (255,0,0) quantises to ANSI bright red (9) = Win32 0x0C */
    WORD attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(attrs & 0x0F, (WORD)0x0C);
}

TEST(dispatch_sgr_combined) {
    TestFixture f;
    /* Bold + red fg + blue bg in a single sequence */
    f.feed("\x1b[1;31;44m");
    f.feed("X");

    WORD attrs = f.cell(0, 0)->attributes;
    /* Red fg with bold = 0x04 | 0x08 = 0x0C */
    ASSERT_EQ(attrs & 0x0F, (WORD)0x0C);
    /* Blue bg = 0x10 */
    ASSERT_EQ(attrs & 0xF0, (WORD)0x10);
}

TEST(dispatch_sgr_no_params_is_reset) {
    TestFixture f;
    f.feed("\x1b[31m");  /* Set red */
    f.feed("\x1b[m");    /* No params = reset */
    f.feed("A");

    ASSERT_EQ(f.cell(0, 0)->attributes, DEFAULT_ATTRS);
}

TEST(dispatch_sgr_default_fg) {
    TestFixture f;
    f.feed("\x1b[31m");  /* Red */
    f.feed("\x1b[39m");  /* Default foreground */
    f.feed("A");

    ASSERT_EQ(f.cell(0, 0)->attributes & 0x0F, DEFAULT_ATTRS & 0x0F);
}

TEST(dispatch_sgr_default_bg) {
    TestFixture f;
    f.feed("\x1b[44m");  /* Blue bg */
    f.feed("\x1b[49m");  /* Default background */
    f.feed("A");

    ASSERT_EQ(f.cell(0, 0)->attributes & 0xF0, DEFAULT_ATTRS & 0xF0);
}

TEST(dispatch_sgr_bold_off) {
    TestFixture f;
    f.feed("\x1b[1m");   /* Bold on */
    f.feed("\x1b[22m");  /* Normal intensity */
    f.feed("A");

    ASSERT_TRUE((f.cell(0, 0)->attributes & FOREGROUND_INTENSITY) == 0);
}

TEST(dispatch_sgr_underline_off) {
    TestFixture f;
    f.feed("\x1b[4m");   /* Underline on */
    f.feed("\x1b[24m");  /* Underline off */
    f.feed("A");

    ASSERT_TRUE((f.cell(0, 0)->attributes & 0x8000) == 0);
}

TEST(dispatch_sgr_italic_off) {
    TestFixture f;
    f.feed("\x1b[3m");   /* Italic on */
    f.feed("\x1b[23m");  /* Italic off */
    f.feed("A");

    ASSERT_TRUE((f.cell(0, 0)->attributes & 0x8000) == 0);
}

TEST(dispatch_sgr_reverse_off) {
    TestFixture f;
    f.feed("\x1b[7m");   /* Reverse on */
    f.feed("\x1b[27m");  /* Reverse off */
    f.feed("A");

    /* Should be back to normal default attrs */
    ASSERT_EQ(f.cell(0, 0)->attributes, DEFAULT_ATTRS);
}

/*============================================================================
 * Tests - Scrolling
 *============================================================================*/

TEST(dispatch_scroll_up) {
    TestFixture f(80, 5);
    f.feed("Line0\r\nLine1\r\nLine2\r\nLine3\r\nLine4");
    f.feed("\x1b[2S");  /* Scroll up 2 */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'L');
    /* Row 0 should now contain what was row 2 */
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'2');
}

TEST(dispatch_scroll_down) {
    TestFixture f(80, 5);
    f.feed("Line0\r\nLine1\r\nLine2");
    f.feed("\x1b[1T");  /* Scroll down 1 */

    /* Row 0 should be blank */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');
    /* Row 1 should have what was row 0 */
    ASSERT_EQ(f.cell(4, 1)->codepoint, (uint32_t)'0');
}

TEST(dispatch_lf_scrolls_at_bottom) {
    TestFixture f(80, 3);
    f.feed("Row0\r\nRow1\r\nRow2");

    /* Cursor is at bottom row. LF should scroll up. */
    f.feed("\n");

    /* Row 0 should now be what was Row 1 */
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)'1');
    /* Row 2 (bottom) should be blank */
    ASSERT_EQ(f.cell(0, 2)->codepoint, (uint32_t)' ');
}

/*============================================================================
 * Tests - Scroll Region (DECSTBM)
 *============================================================================*/

TEST(dispatch_scroll_region) {
    TestFixture f(80, 10);
    /* Set scroll region to rows 3-7 (1-based) */
    f.feed("\x1b[3;7r");

    ASSERT_EQ(f.screen->scroll_top, 2);
    ASSERT_EQ(f.screen->scroll_bottom, 6);
    /* DECSTBM homes the cursor */
    ASSERT_EQ(f.screen->cursor.x, 0);
    ASSERT_EQ(f.screen->cursor.y, 0);
}

TEST(dispatch_scroll_region_reset) {
    TestFixture f(80, 10);
    f.feed("\x1b[3;7r");   /* Set region */
    f.feed("\x1b[r");      /* Reset region (defaults: 1;height) */

    ASSERT_EQ(f.screen->scroll_top, 0);
    ASSERT_EQ(f.screen->scroll_bottom, 9);
}

/*============================================================================
 * Tests - Insert / Delete Lines
 *============================================================================*/

TEST(dispatch_insert_lines) {
    TestFixture f(80, 5);
    f.feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    f.feed("\x1b[2;1H");  /* Cursor to row 2 */
    f.feed("\x1b[1L");    /* Insert 1 line */

    /* Row 1 should be blank (inserted) */
    ASSERT_EQ(f.cell(0, 1)->codepoint, (uint32_t)' ');
    /* Row 2 should have what was row 1 (BBB) */
    ASSERT_EQ(f.cell(0, 2)->codepoint, (uint32_t)'B');
}

TEST(dispatch_delete_lines) {
    TestFixture f(80, 5);
    f.feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    f.feed("\x1b[2;1H");  /* Cursor to row 2 */
    f.feed("\x1b[1M");    /* Delete 1 line */

    /* Row 1 should now have CCC (what was row 2) */
    ASSERT_EQ(f.cell(0, 1)->codepoint, (uint32_t)'C');
}

/*============================================================================
 * Tests - Insert / Delete Characters
 *============================================================================*/

TEST(dispatch_insert_chars) {
    TestFixture f;
    f.feed("ABCDE");
    f.feed("\x1b[3G");    /* Column 3 (0-based: 2) */
    f.feed("\x1b[2@");    /* Insert 2 chars */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'B');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)' ');  /* Inserted */
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)' ');  /* Inserted */
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'C');
    ASSERT_EQ(f.cell(5, 0)->codepoint, (uint32_t)'D');
}

TEST(dispatch_delete_chars) {
    TestFixture f;
    f.feed("ABCDE");
    f.feed("\x1b[2G");    /* Column 2 (0-based: 1) */
    f.feed("\x1b[2P");    /* Delete 2 chars */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'D');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)'E');
}

/*============================================================================
 * Tests - Cursor Save/Restore
 *============================================================================*/

TEST(dispatch_save_restore_csi) {
    /* CSI s / CSI u */
    TestFixture f;
    f.feed("\x1b[5;10H");  /* Position */
    f.feed("\x1b[s");      /* Save */
    f.feed("\x1b[1;1H");   /* Move home */
    f.feed("\x1b[u");      /* Restore */

    ASSERT_EQ(f.screen->cursor.x, 9);
    ASSERT_EQ(f.screen->cursor.y, 4);
}

TEST(dispatch_save_restore_esc) {
    /* ESC 7 / ESC 8 (DECSC/DECRC) */
    TestFixture f;
    f.feed("\x1b[5;10H");
    f.feed("\x1b""7");     /* DECSC */
    f.feed("\x1b[1;1H");
    f.feed("\x1b""8");     /* DECRC */

    ASSERT_EQ(f.screen->cursor.x, 9);
    ASSERT_EQ(f.screen->cursor.y, 4);
}

/*============================================================================
 * Tests - ESC Sequences
 *============================================================================*/

TEST(dispatch_reverse_index) {
    /* ESC M (RI): move cursor up, scroll down if at top */
    TestFixture f(80, 5);
    f.feed("Line0\r\nLine1");
    f.feed("\x1b[1;1H");  /* Move to row 1 */
    f.feed("\x1bM");      /* RI */

    /* Already at top of scroll region (row 0), should scroll down */
    ASSERT_EQ(f.screen->cursor.y, 0);
    /* Row 0 should now be blank (scrolled down) */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');
    /* Row 1 should contain "Line0" */
    ASSERT_EQ(f.cell(4, 1)->codepoint, (uint32_t)'0');
}

TEST(dispatch_ind) {
    /* ESC D (IND): index, same as LF */
    TestFixture f;
    f.feed("A");
    f.feed("\x1b""D");

    ASSERT_EQ(f.screen->cursor.y, 1);
}

TEST(dispatch_nel) {
    /* ESC E (NEL): next line (CR + LF) */
    TestFixture f;
    f.feed("ABCDE");
    f.feed("\x1b""E");

    ASSERT_EQ(f.screen->cursor.x, 0);
    ASSERT_EQ(f.screen->cursor.y, 1);
}

TEST(dispatch_ris) {
    /* ESC c (RIS): full reset */
    TestFixture f;
    f.feed("\x1b[31m");  /* Set colour */
    f.feed("Hello");
    f.feed("\x1b[5;10H");
    f.feed("\x1b""c");     /* RIS */

    /* Screen should be cleared */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');
    /* Cursor should be home */
    ASSERT_EQ(f.screen->cursor.x, 0);
    ASSERT_EQ(f.screen->cursor.y, 0);
    /* Attributes should be default */
    ASSERT_EQ(f.screen->current_attrs, DEFAULT_ATTRS);
    /* Cursor visible */
    ASSERT_TRUE(f.screen->cursor.visible);
}

/*============================================================================
 * Tests - Private Modes (DECSET/DECRST)
 *============================================================================*/

TEST(dispatch_cursor_visibility) {
    TestFixture f;
    ASSERT_TRUE(f.screen->cursor.visible);

    f.feed("\x1b[?25l");  /* Hide cursor */
    ASSERT_FALSE(f.screen->cursor.visible);

    f.feed("\x1b[?25h");  /* Show cursor */
    ASSERT_TRUE(f.screen->cursor.visible);
}

TEST(dispatch_alternate_screen) {
    TestFixture f;
    f.feed("Main screen text");

    f.feed("\x1b[?1049h");  /* Enter alternate */
    ASSERT_TRUE(f.screen->using_alternate);

    /* Alternate screen should be blank */
    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_NE(alt, (WPScreenBuffer*)NULL);
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->codepoint, (uint32_t)' ');

    f.feed("\x1b[?1049l");  /* Leave alternate */
    ASSERT_FALSE(f.screen->using_alternate);
}

/*============================================================================
 * Tests - REP (Repeat Character)
 *============================================================================*/

TEST(dispatch_rep_basic) {
    TestFixture f;
    f.feed("X");
    f.feed("\x1b[3b");  /* Repeat 'X' 3 times */

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'X');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'X');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)'X');
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)'X');
    ASSERT_EQ(f.screen->cursor.x, 4);
}

TEST(dispatch_rep_no_preceding) {
    /* REP with no preceding print should be a no-op */
    TestFixture f;
    f.feed("\x1b[5b");

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');
    ASSERT_EQ(f.screen->cursor.x, 0);
}

/*============================================================================
 * Tests - Device Status Report (DSR)
 *============================================================================*/

TEST(dispatch_dsr_status) {
    TestFixture f;
    WriteBackRecorder rec;
    wp_dispatch_set_write_back(&f.dispatch, WriteBackRecorder::callback, &rec);

    f.feed("\x1b[5n");  /* DSR: status */

    /* Should respond with ESC [ 0 n */
    ASSERT_EQ(rec.as_string(), std::string("\x1b[0n"));
}

TEST(dispatch_dsr_cursor_position) {
    TestFixture f;
    WriteBackRecorder rec;
    wp_dispatch_set_write_back(&f.dispatch, WriteBackRecorder::callback, &rec);

    f.feed("\x1b[5;10H");  /* Move to row 5, col 10 */
    f.feed("\x1b[6n");     /* DSR: cursor position */

    /* Should respond with ESC [ 5 ; 10 R (1-based) */
    ASSERT_EQ(rec.as_string(), std::string("\x1b[5;10R"));
}

TEST(dispatch_dsr_no_writeback) {
    /* DSR without a write-back callback should not crash */
    TestFixture f;
    f.feed("\x1b[6n");
    /* Just verify it doesn't crash - no assertion needed */
    ASSERT_TRUE(true);
}

/*============================================================================
 * Tests - Window Operations (XTWINOPS)
 *============================================================================*/

TEST(dispatch_winops_report_size) {
    /* CSI 18 t should respond with CSI 8 ; rows ; cols t */
    TestFixture f;
    WriteBackRecorder rec;
    wp_dispatch_set_write_back(&f.dispatch, WriteBackRecorder::callback, &rec);

    f.feed("\x1b[18t");

    /* Default screen is 80x24, so expect ESC [ 8 ; 24 ; 80 t */
    ASSERT_EQ(rec.as_string(), std::string("\x1b[8;24;80t"));
}

TEST(dispatch_winops_no_writeback) {
    /* CSI 18 t without a write-back callback should not crash */
    TestFixture f;
    f.feed("\x1b[18t");
    ASSERT_TRUE(true);
}

TEST(dispatch_winops_unknown_ignored) {
    /* An unrecognised sub-command should not produce a response */
    TestFixture f;
    WriteBackRecorder rec;
    wp_dispatch_set_write_back(&f.dispatch, WriteBackRecorder::callback, &rec);

    f.feed("\x1b[99t");

    ASSERT_EQ(rec.as_string(), std::string(""));
}

TEST(dispatch_osc_window_title) {
    TestFixture f;

    /* If title APIs are unavailable in this environment, skip gracefully. */
    if (!SetConsoleTitleW(L"WinPatinaPreTitle")) {
        ASSERT_TRUE(true);
        return;
    }

    f.feed("\x1b]2;WinPatina OSC Title Test\x07");

    WCHAR got[256];
    DWORD n = GetConsoleTitleW(got, 256);
    if (n == 0) {
        ASSERT_TRUE(true);
        return;
    }

    ASSERT_TRUE(wcscmp(got, L"WinPatina OSC Title Test") == 0);

    /* Restore to a neutral title for subsequent tests. */
    SetConsoleTitleW(L"");
}

/*============================================================================
 * Tests - Full Pipeline Integration
 *============================================================================*/

TEST(dispatch_coloured_text) {
    /* Full pipeline: set colour, write text, verify both content and attrs */
    TestFixture f;
    f.feed("\x1b[1;32mGreen\x1b[0m Normal");

    /* "Green" should have bright green foreground */
    WORD green_attrs = f.cell(0, 0)->attributes;
    ASSERT_EQ(green_attrs & 0x0F, (WORD)0x0A);  /* Bright green: 0x02 | 0x08 */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'G');
    ASSERT_EQ(f.cell(4, 0)->codepoint, (uint32_t)'n');

    /* " Normal" should have default attrs */
    ASSERT_EQ(f.cell(5, 0)->attributes, DEFAULT_ATTRS);
    ASSERT_EQ(f.cell(6, 0)->codepoint, (uint32_t)'N');
}

TEST(dispatch_multiline_with_attributes) {
    TestFixture f(80, 5);
    f.feed("\x1b[31mRed\x1b[0m\r\n\x1b[34mBlue\x1b[0m");

    /* Row 0: "Red" in red */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'R');
    ASSERT_EQ(f.cell(0, 0)->attributes & 0x0F, (WORD)0x04);

    /* Row 1: "Blue" in blue */
    ASSERT_EQ(f.cell(0, 1)->codepoint, (uint32_t)'B');
    ASSERT_EQ(f.cell(0, 1)->attributes & 0x0F, (WORD)0x01);
}

TEST(dispatch_clear_and_redraw) {
    TestFixture f;
    f.feed("Old text here");
    f.feed("\x1b[2J\x1b[H");  /* Clear screen + home */
    f.feed("New");

    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'N');
    ASSERT_EQ(f.cell(1, 0)->codepoint, (uint32_t)'e');
    ASSERT_EQ(f.cell(2, 0)->codepoint, (uint32_t)'w');
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)' ');
}

/*============================================================================
 * Tests - Alternate Buffer Dispatch Routing
 *============================================================================*/

TEST(dispatch_alt_print_goes_to_alternate) {
    TestFixture f;
    f.feed("Main");
    f.feed("\x1b[?1049h");  /* Enter alternate */
    f.feed("Alt");

    /* Text should be on the alternate buffer, not the main */
    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_NE(alt, (WPScreenBuffer*)NULL);
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->codepoint, (uint32_t)'A');
    ASSERT_EQ(wp_screen_cell_at(alt, 1, 0)->codepoint, (uint32_t)'l');
    ASSERT_EQ(wp_screen_cell_at(alt, 2, 0)->codepoint, (uint32_t)'t');

    /* Main buffer should still have "Main" */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'M');
    ASSERT_EQ(f.cell(3, 0)->codepoint, (uint32_t)'n');

    f.feed("\x1b[?1049l");  /* Leave alternate */
}

TEST(dispatch_alt_cursor_movement) {
    TestFixture f;
    f.feed("\x1b[?1049h");  /* Enter alternate */
    f.feed("\x1b[5;10H");   /* CUP to row 5, col 10 */

    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_EQ(alt->cursor.x, 9);
    ASSERT_EQ(alt->cursor.y, 4);

    /* Main cursor should be at saved position (0,0 from before enter) */
    ASSERT_EQ(f.screen->cursor.saved_x, 0);
    ASSERT_EQ(f.screen->cursor.saved_y, 0);

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_erase) {
    TestFixture f;
    f.feed("\x1b[?1049h");  /* Enter alternate */
    f.feed("Hello");
    f.feed("\x1b[2J");      /* Erase display */

    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->codepoint, (uint32_t)' ');

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_sgr_attrs) {
    TestFixture f;
    f.feed("\x1b[?1049h");   /* Enter alternate */
    f.feed("\x1b[31m");      /* Red foreground */
    f.feed("R");

    WPScreenBuffer* alt = f.screen->alternate;
    /* Red fg = Win32 0x04 */
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->attributes & 0x0F, (WORD)0x04);

    /* Main buffer's current_attrs should still be default */
    /* (main is not affected by SGR while alternate is active) */

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_scroll) {
    TestFixture f(80, 5);
    f.feed("\x1b[?1049h");  /* Enter alternate */

    /* Fill alternate with lines */
    f.feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    f.feed("\x1b[2S");  /* Scroll up 2 */

    WPScreenBuffer* alt = f.screen->alternate;
    /* Row 0 should now be what was row 2 (CCC) */
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->codepoint, (uint32_t)'C');

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_c0_controls) {
    TestFixture f;
    f.feed("\x1b[?1049h");  /* Enter alternate */
    f.feed("ABCDE\rX");     /* CR should go to col 0 on alternate */

    WPScreenBuffer* alt = f.screen->alternate;
    /* 'X' should overwrite 'A' at col 0 */
    ASSERT_EQ(wp_screen_cell_at(alt, 0, 0)->codepoint, (uint32_t)'X');
    /* Main should be unaffected */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)' ');

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_esc_sequences) {
    TestFixture f;
    f.feed("\x1b[?1049h");     /* Enter alternate */
    f.feed("Hello");
    f.feed("\x1b""7");         /* DECSC - save cursor on alternate */
    f.feed("\x1b[1;1H");      /* Home */
    f.feed("\x1b""8");         /* DECRC - restore cursor on alternate */

    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_EQ(alt->cursor.x, 5);
    ASSERT_EQ(alt->cursor.y, 0);

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_cursor_visibility) {
    TestFixture f;
    f.feed("\x1b[?1049h");     /* Enter alternate */
    f.feed("\x1b[?25l");       /* Hide cursor */

    WPScreenBuffer* alt = f.screen->alternate;
    ASSERT_FALSE(alt->cursor.visible);

    /* Main cursor visibility should be unaffected */
    ASSERT_TRUE(f.screen->cursor.visible);

    f.feed("\x1b[?1049l");
}

TEST(dispatch_alt_main_preserved_after_leave) {
    TestFixture f;
    f.feed("Original");
    f.feed("\x1b[5;1H");       /* Move cursor to row 5 */

    f.feed("\x1b[?1049h");     /* Enter alternate */
    f.feed("\x1b[31m");        /* Set colour */
    f.feed("Alternate stuff");
    f.feed("\x1b[?1049l");     /* Leave alternate */

    /* Main buffer should have "Original" intact */
    ASSERT_EQ(f.cell(0, 0)->codepoint, (uint32_t)'O');
    ASSERT_EQ(f.cell(7, 0)->codepoint, (uint32_t)'l');

    /* Cursor should be restored to where it was before entering alt */
    ASSERT_EQ(f.screen->cursor.x, 0);
    ASSERT_EQ(f.screen->cursor.y, 4);
}

TEST(dispatch_alt_dsr_reports_alt_cursor) {
    TestFixture f;
    WriteBackRecorder rec;
    wp_dispatch_set_write_back(&f.dispatch, WriteBackRecorder::callback, &rec);

    f.feed("\x1b[?1049h");     /* Enter alternate */
    f.feed("\x1b[3;7H");       /* Cursor to row 3, col 7 on alternate */
    f.feed("\x1b[6n");         /* DSR cursor position */

    /* Should report alternate cursor, not main */
    ASSERT_EQ(rec.as_string(), std::string("\x1b[3;7R"));

    f.feed("\x1b[?1049l");
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
