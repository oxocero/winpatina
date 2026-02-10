/**
 * @file test_colour.cpp
 * @brief Unit tests for the colour mapping utilities
 */

#include "test_harness.h"
#include "../src/winpatina_colour.h"

/*============================================================================
 * Tests - ANSI 16 to Win32 Foreground
 *============================================================================*/

TEST(fg_black) {
    ASSERT_EQ(wp_colour_ansi_to_fg(0), (WORD)0x00);
}

TEST(fg_red) {
    /* ANSI red = Win32 FOREGROUND_RED (0x04) due to BGR ordering */
    ASSERT_EQ(wp_colour_ansi_to_fg(1), (WORD)0x04);
}

TEST(fg_green) {
    ASSERT_EQ(wp_colour_ansi_to_fg(2), (WORD)0x02);
}

TEST(fg_yellow) {
    /* Yellow = RED | GREEN */
    ASSERT_EQ(wp_colour_ansi_to_fg(3), (WORD)0x06);
}

TEST(fg_blue) {
    ASSERT_EQ(wp_colour_ansi_to_fg(4), (WORD)0x01);
}

TEST(fg_magenta) {
    ASSERT_EQ(wp_colour_ansi_to_fg(5), (WORD)0x05);
}

TEST(fg_cyan) {
    ASSERT_EQ(wp_colour_ansi_to_fg(6), (WORD)0x03);
}

TEST(fg_white) {
    ASSERT_EQ(wp_colour_ansi_to_fg(7), (WORD)0x07);
}

TEST(fg_bright_red) {
    /* Bright colours have INTENSITY bit (0x08) */
    ASSERT_EQ(wp_colour_ansi_to_fg(9), (WORD)0x0C);
}

TEST(fg_bright_white) {
    ASSERT_EQ(wp_colour_ansi_to_fg(15), (WORD)0x0F);
}

TEST(fg_out_of_range_low) {
    /* Negative index defaults to white (0x07) */
    ASSERT_EQ(wp_colour_ansi_to_fg(-1), (WORD)0x07);
}

TEST(fg_out_of_range_high) {
    ASSERT_EQ(wp_colour_ansi_to_fg(16), (WORD)0x07);
}

/*============================================================================
 * Tests - ANSI 16 to Win32 Background
 *============================================================================*/

TEST(bg_black) {
    ASSERT_EQ(wp_colour_ansi_to_bg(0), (WORD)0x00);
}

TEST(bg_red) {
    /* Background is shifted left 4 bits: RED = 0x40 */
    ASSERT_EQ(wp_colour_ansi_to_bg(1), (WORD)0x40);
}

TEST(bg_white) {
    ASSERT_EQ(wp_colour_ansi_to_bg(7), (WORD)0x70);
}

TEST(bg_bright_white) {
    ASSERT_EQ(wp_colour_ansi_to_bg(15), (WORD)0xF0);
}

TEST(bg_out_of_range) {
    ASSERT_EQ(wp_colour_ansi_to_bg(-1), (WORD)0x00);
    ASSERT_EQ(wp_colour_ansi_to_bg(16), (WORD)0x00);
}

/*============================================================================
 * Tests - 256-Colour to RGB
 *============================================================================*/

TEST(colour256_standard_black) {
    WPColourRGB c = wp_colour_256_to_rgb(0);
    ASSERT_EQ(c.r, (uint8_t)0);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);
}

TEST(colour256_standard_red) {
    WPColourRGB c = wp_colour_256_to_rgb(1);
    ASSERT_EQ(c.r, (uint8_t)128);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);
}

TEST(colour256_standard_bright_white) {
    WPColourRGB c = wp_colour_256_to_rgb(15);
    ASSERT_EQ(c.r, (uint8_t)255);
    ASSERT_EQ(c.g, (uint8_t)255);
    ASSERT_EQ(c.b, (uint8_t)255);
}

TEST(colour256_cube_origin) {
    /* Index 16 = cube (0,0,0) = black */
    WPColourRGB c = wp_colour_256_to_rgb(16);
    ASSERT_EQ(c.r, (uint8_t)0);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);
}

TEST(colour256_cube_red_max) {
    /* Index 196 = cube (5,0,0) = pure bright red
     * ci = 196 - 16 = 180, ri = 180/36 = 5, gi = 0, bi = 0
     * r = 55 + 5*40 = 255 */
    WPColourRGB c = wp_colour_256_to_rgb(196);
    ASSERT_EQ(c.r, (uint8_t)255);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);
}

TEST(colour256_cube_index_17) {
    /* Index 17 = cube (0,0,1) = blue component = 95
     * ci = 1, ri = 0, gi = 0, bi = 1
     * b = 55 + 1*40 = 95 */
    WPColourRGB c = wp_colour_256_to_rgb(17);
    ASSERT_EQ(c.r, (uint8_t)0);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)95);
}

TEST(colour256_cube_white) {
    /* Index 231 = cube (5,5,5) = bright white
     * ci = 215, ri = 5, gi = 5, bi = 5
     * all = 55 + 5*40 = 255 */
    WPColourRGB c = wp_colour_256_to_rgb(231);
    ASSERT_EQ(c.r, (uint8_t)255);
    ASSERT_EQ(c.g, (uint8_t)255);
    ASSERT_EQ(c.b, (uint8_t)255);
}

TEST(colour256_greyscale_first) {
    /* Index 232 = first greyscale = 8 */
    WPColourRGB c = wp_colour_256_to_rgb(232);
    ASSERT_EQ(c.r, (uint8_t)8);
    ASSERT_EQ(c.g, (uint8_t)8);
    ASSERT_EQ(c.b, (uint8_t)8);
}

TEST(colour256_greyscale_last) {
    /* Index 255 = last greyscale = 8 + 23*10 = 238 */
    WPColourRGB c = wp_colour_256_to_rgb(255);
    ASSERT_EQ(c.r, (uint8_t)238);
    ASSERT_EQ(c.g, (uint8_t)238);
    ASSERT_EQ(c.b, (uint8_t)238);
}

TEST(colour256_greyscale_mid) {
    /* Index 244 = 8 + 12*10 = 128 */
    WPColourRGB c = wp_colour_256_to_rgb(244);
    ASSERT_EQ(c.r, (uint8_t)128);
    ASSERT_EQ(c.g, (uint8_t)128);
    ASSERT_EQ(c.b, (uint8_t)128);
}

TEST(colour256_out_of_range) {
    WPColourRGB c = wp_colour_256_to_rgb(-1);
    ASSERT_EQ(c.r, (uint8_t)0);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);

    c = wp_colour_256_to_rgb(256);
    ASSERT_EQ(c.r, (uint8_t)0);
    ASSERT_EQ(c.g, (uint8_t)0);
    ASSERT_EQ(c.b, (uint8_t)0);
}

/*============================================================================
 * Tests - RGB to 16-Colour Quantisation
 *============================================================================*/

TEST(rgb_to_16_exact_black) {
    ASSERT_EQ(wp_colour_rgb_to_16(0, 0, 0), 0);
}

TEST(rgb_to_16_exact_red) {
    ASSERT_EQ(wp_colour_rgb_to_16(128, 0, 0), 1);
}

TEST(rgb_to_16_exact_bright_white) {
    ASSERT_EQ(wp_colour_rgb_to_16(255, 255, 255), 15);
}

TEST(rgb_to_16_exact_bright_green) {
    ASSERT_EQ(wp_colour_rgb_to_16(0, 255, 0), 10);
}

TEST(rgb_to_16_near_red) {
    /* Slightly off-red should still match red */
    ASSERT_EQ(wp_colour_rgb_to_16(140, 10, 10), 1);
}

TEST(rgb_to_16_near_bright_blue) {
    /* Close to bright blue (0, 0, 255) */
    ASSERT_EQ(wp_colour_rgb_to_16(10, 10, 240), 12);
}

TEST(rgb_to_16_grey_matches_bright_black) {
    /* Mid-grey (128, 128, 128) should match bright black / grey (index 8) */
    ASSERT_EQ(wp_colour_rgb_to_16(128, 128, 128), 8);
}

TEST(rgb_to_16_clamping) {
    /* Values outside 0-255 should be clamped */
    int result = wp_colour_rgb_to_16(-10, 300, 128);
    ASSERT_TRUE(result >= 0 && result <= 15);
}

/*============================================================================
 * Tests - 256-Colour to 16-Colour
 *============================================================================*/

TEST(colour256_to_16_direct) {
    /* Indices 0-15 map directly */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(wp_colour_256_to_16(i), i);
    }
}

TEST(colour256_to_16_cube_red) {
    /* Index 196 = bright red in cube, should map to bright red (9) */
    ASSERT_EQ(wp_colour_256_to_16(196), 9);
}

TEST(colour256_to_16_cube_green) {
    /* Index 46 = (0,5,0) = bright green, should map to bright green (10) */
    ASSERT_EQ(wp_colour_256_to_16(46), 10);
}

TEST(colour256_to_16_out_of_range) {
    ASSERT_EQ(wp_colour_256_to_16(-1), 7);
    ASSERT_EQ(wp_colour_256_to_16(256), 7);
}

/*============================================================================
 * Tests - SGR State Management
 *============================================================================*/

TEST(sgr_init_defaults) {
    WPSGRState sgr;
    /* Default attrs: white on black (0x07) */
    wp_sgr_init(&sgr, 0x07);

    ASSERT_EQ(sgr.fg_index, 7);
    ASSERT_EQ(sgr.bg_index, 0);
    ASSERT_FALSE(sgr.bold);
    ASSERT_FALSE(sgr.dim);
    ASSERT_FALSE(sgr.italic);
    ASSERT_FALSE(sgr.underline);
    ASSERT_FALSE(sgr.reverse);
}

TEST(sgr_init_with_intensity) {
    WPSGRState sgr;
    /* Default attrs: bright white on black (0x0F) */
    wp_sgr_init(&sgr, 0x0F);

    ASSERT_EQ(sgr.fg_index, 7);
    ASSERT_TRUE(sgr.bold);
}

TEST(sgr_reset_restores_defaults) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* Modify state */
    sgr.fg_index = 1;
    sgr.bg_index = 4;
    sgr.bold = true;
    sgr.italic = true;
    sgr.underline = true;
    sgr.reverse = true;

    /* Reset should restore to init state */
    wp_sgr_reset(&sgr, 0x07);

    ASSERT_EQ(sgr.fg_index, 7);
    ASSERT_EQ(sgr.bg_index, 0);
    ASSERT_FALSE(sgr.bold);
    ASSERT_FALSE(sgr.italic);
    ASSERT_FALSE(sgr.underline);
    ASSERT_FALSE(sgr.reverse);
}

TEST(sgr_to_attrs_basic_fg) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* Set foreground to red (ANSI 1 = Win32 0x04) */
    sgr.fg_index = 1;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    ASSERT_EQ(attrs & 0x0F, (WORD)0x04);
}

TEST(sgr_to_attrs_basic_bg) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* Set background to blue (ANSI 4 = Win32 bg 0x10) */
    sgr.bg_index = 4;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    ASSERT_EQ(attrs & 0xF0, (WORD)0x10);
}

TEST(sgr_to_attrs_bold_intensity) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    sgr.bold = true;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    ASSERT_TRUE((attrs & FOREGROUND_INTENSITY) != 0);
}

TEST(sgr_to_attrs_dim_default_white_to_grey) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);  /* Default white on black */

    sgr.dim = true;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);

    /* Faint default white should become grey (ANSI 8 -> Win32 0x08). */
    ASSERT_EQ(attrs & 0x0F, (WORD)0x08);
}

TEST(sgr_to_attrs_dim_bright_red_to_red) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* Bright red should dim to normal red. */
    sgr.fg_index = 9;  /* ANSI bright red */
    sgr.dim = true;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);

    ASSERT_EQ(attrs & 0x0F, (WORD)0x04);
}

TEST(sgr_to_attrs_underline_with_lvb) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    sgr.underline = true;
    WORD attrs_with = wp_sgr_to_attrs(&sgr, 0x07, true);
    WORD attrs_without = wp_sgr_to_attrs(&sgr, 0x07, false);

    /* Underline only set when has_lvb is true */
    ASSERT_TRUE((attrs_with & 0x8000) != 0);
    ASSERT_TRUE((attrs_without & 0x8000) == 0);
}

TEST(sgr_to_attrs_italic_fallback_with_lvb) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    sgr.italic = true;
    WORD attrs_with = wp_sgr_to_attrs(&sgr, 0x07, true);
    WORD attrs_without = wp_sgr_to_attrs(&sgr, 0x07, false);

    ASSERT_TRUE((attrs_with & 0x8000) != 0);
    ASSERT_TRUE((attrs_without & 0x8000) == 0);
}

TEST(sgr_to_attrs_reverse) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* Red fg (0x04) on blue bg (0x10) reversed should become
     * blue fg (0x01) on red bg (0x40) */
    sgr.fg_index = 1;  /* Red: fg bits = 0x04 */
    sgr.bg_index = 4;  /* Blue: bg bits = 0x10 */
    sgr.reverse = true;

    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    /* After reverse: fg = bg>>4 = 0x01, bg = (fg&0x0F)<<4 = 0x40 */
    ASSERT_EQ(attrs & 0x0F, (WORD)0x01);
    ASSERT_EQ(attrs & 0xF0, (WORD)0x40);
}

TEST(sgr_to_attrs_default_fg) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* fg_index = -1 means use default foreground */
    sgr.fg_index = -1;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    ASSERT_EQ(attrs & 0x0F, (WORD)0x07);
}

TEST(sgr_to_attrs_default_bg) {
    WPSGRState sgr;
    wp_sgr_init(&sgr, 0x07);

    /* bg_index = -1 means use default background */
    sgr.bg_index = -1;
    WORD attrs = wp_sgr_to_attrs(&sgr, 0x07, false);
    ASSERT_EQ(attrs & 0xF0, (WORD)0x00);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
