/**
 * @file test_input.cpp
 * @brief Unit tests for the console input handler
 *
 * Tests translation of Win32 console input events (keyboard, mouse)
 * into VT escape sequences. All tests construct KEY_EVENT_RECORD or
 * MOUSE_EVENT_RECORD structs directly — no real console needed.
 */

#include "test_harness.h"
#include "../src/winpatina_input.h"

#include <cstring>

/*============================================================================
 * Test Helpers
 *============================================================================*/

/** Build a KEY_EVENT_RECORD for a key-down with the given params. */
static KEY_EVENT_RECORD make_key(WORD vk, WCHAR uc, DWORD ctrl = 0)
{
    KEY_EVENT_RECORD k;
    memset(&k, 0, sizeof(k));
    k.bKeyDown = TRUE;
    k.wRepeatCount = 1;
    k.wVirtualKeyCode = vk;
    k.uChar.UnicodeChar = uc;
    k.dwControlKeyState = ctrl;
    return k;
}

/** Build a key-up event. */
static KEY_EVENT_RECORD make_key_up(WORD vk, WCHAR uc, DWORD ctrl = 0)
{
    KEY_EVENT_RECORD k = make_key(vk, uc, ctrl);
    k.bKeyDown = FALSE;
    return k;
}

/** Build a MOUSE_EVENT_RECORD. */
static MOUSE_EVENT_RECORD make_mouse(SHORT x, SHORT y,
                                      DWORD buttons, DWORD event_flags,
                                      DWORD ctrl = 0)
{
    MOUSE_EVENT_RECORD m;
    memset(&m, 0, sizeof(m));
    m.dwMousePosition.X = x;
    m.dwMousePosition.Y = y;
    m.dwButtonState = buttons;
    m.dwEventFlags = event_flags;
    m.dwControlKeyState = ctrl;
    return m;
}

/** Compare output buffer against an expected string. */
static bool buf_eq(const uint8_t* buf, int len, const char* expected)
{
    int elen = (int)strlen(expected);
    if (len != elen) return false;
    return memcmp(buf, expected, len) == 0;
}

/*============================================================================
 * Tests - Lifecycle
 *============================================================================*/

TEST(input_init) {
    WPInputState state;
    wp_input_init(&state);

    ASSERT_EQ(state.cursor_key_mode, WP_CURSOR_KEY_NORMAL);
    ASSERT_EQ(state.keypad_mode, WP_KEYPAD_NUMERIC);
    ASSERT_EQ(state.mouse_mode, WP_MOUSE_OFF);
    ASSERT_EQ(state.mouse_encoding, WP_MOUSE_ENC_DEFAULT);
    ASSERT_FALSE(state.bracketed_paste);
    ASSERT_EQ(state.prev_mouse_buttons, (DWORD)0);
}

TEST(input_init_null_safe) {
    wp_input_init(NULL);
    ASSERT_TRUE(true);
}

/*============================================================================
 * Tests - Null Safety
 *============================================================================*/

TEST(input_key_null_params) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('A', 'a');

    ASSERT_EQ(wp_input_translate_key(NULL, &k, buf), 0);
    ASSERT_EQ(wp_input_translate_key(&state, NULL, buf), 0);
    ASSERT_EQ(wp_input_translate_key(&state, &k, NULL), 0);
}

TEST(input_mouse_null_params) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    MOUSE_EVENT_RECORD m = make_mouse(0, 0, 0, 0);

    ASSERT_EQ(wp_input_translate_mouse(NULL, &m, buf), 0);
    ASSERT_EQ(wp_input_translate_mouse(&state, NULL, buf), 0);
    ASSERT_EQ(wp_input_translate_mouse(&state, &m, NULL), 0);
}

/*============================================================================
 * Tests - Key-Up Ignored
 *============================================================================*/

TEST(input_key_up_ignored) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key_up('A', 'a');
    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
}

/*============================================================================
 * Tests - Modifier-Only Keys
 *============================================================================*/

TEST(input_modifier_only_keys) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k;

    k = make_key(VK_SHIFT, 0);   ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_CONTROL, 0); ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_MENU, 0);    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_CAPITAL, 0); ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_NUMLOCK, 0); ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_LWIN, 0);    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
    k = make_key(VK_RWIN, 0);    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
}

/*============================================================================
 * Tests - Printable Characters
 *============================================================================*/

TEST(input_ascii_char) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key('A', 'a');
    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)'a');
}

TEST(input_uppercase_char) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key('A', 'A', SHIFT_PRESSED);
    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)'A');
}

TEST(input_bmp_char) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key(0, 0x20AC);  /* Euro sign U+20AC */
    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 3);
    ASSERT_EQ(buf[0], (uint8_t)0xE2);
    ASSERT_EQ(buf[1], (uint8_t)0x82);
    ASSERT_EQ(buf[2], (uint8_t)0xAC);
}

TEST(input_two_byte_utf8) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key(0, 0x00E1);  /* a-acute U+00E1 */
    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], (uint8_t)0xC3);
    ASSERT_EQ(buf[1], (uint8_t)0xA1);
}

TEST(input_no_unicode_char) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key(0xFF, 0);
    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
}

/*============================================================================
 * Tests - Arrow Keys (Normal Mode)
 *============================================================================*/

TEST(input_arrow_up) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[A"));
}

TEST(input_arrow_down) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_DOWN, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[B"));
}

TEST(input_arrow_right) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_RIGHT, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[C"));
}

TEST(input_arrow_left) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_LEFT, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[D"));
}

/*============================================================================
 * Tests - Arrow Keys (Application Mode)
 *============================================================================*/

TEST(input_arrow_up_app) {
    WPInputState state;
    wp_input_init(&state);
    state.cursor_key_mode = WP_CURSOR_KEY_APPLICATION;
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOA"));
}

TEST(input_arrow_down_app) {
    WPInputState state;
    wp_input_init(&state);
    state.cursor_key_mode = WP_CURSOR_KEY_APPLICATION;
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_DOWN, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOB"));
}

/*============================================================================
 * Tests - Arrow Keys with Modifiers
 *============================================================================*/

TEST(input_arrow_up_shift) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0, SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;2A"));
}

TEST(input_arrow_up_ctrl) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;5A"));
}

TEST(input_arrow_up_alt) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0, LEFT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;3A"));
}

TEST(input_arrow_up_ctrl_shift) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0, LEFT_CTRL_PRESSED | SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;6A"));
}

TEST(input_arrow_modified_app_uses_csi) {
    WPInputState state;
    wp_input_init(&state);
    state.cursor_key_mode = WP_CURSOR_KEY_APPLICATION;
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_UP, 0, SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;2A"));
}

/*============================================================================
 * Tests - Home / End
 *============================================================================*/

TEST(input_home) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_HOME, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[H"));
}

TEST(input_end) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_END, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[F"));
}

TEST(input_home_ctrl) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_HOME, 0, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;5H"));
}

/*============================================================================
 * Tests - Editing Keys (Tilde Style)
 *============================================================================*/

TEST(input_insert) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_INSERT, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[2~"));
}

TEST(input_delete) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_DELETE, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[3~"));
}

TEST(input_page_up) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_PRIOR, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[5~"));
}

TEST(input_page_down) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_NEXT, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[6~"));
}

TEST(input_delete_shift) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_DELETE, 0, SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[3;2~"));
}

/*============================================================================
 * Tests - Function Keys F1-F4 (SS3 Style)
 *============================================================================*/

TEST(input_f1) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F1, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOP"));
}

TEST(input_f2) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F2, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOQ"));
}

TEST(input_f3) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F3, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOR"));
}

TEST(input_f4) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F4, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1bOS"));
}

TEST(input_f1_ctrl) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F1, 0, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[1;5P"));
}

/*============================================================================
 * Tests - Function Keys F5-F12 (Tilde Style)
 *============================================================================*/

TEST(input_f5) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F5, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[15~"));
}

TEST(input_f6) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F6, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[17~"));
}

TEST(input_f12) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F12, 0);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[24~"));
}

TEST(input_f5_shift) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_F5, 0, SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[15;2~"));
}

/*============================================================================
 * Tests - Special Keys (Backspace, Tab, Enter, Escape)
 *============================================================================*/

TEST(input_backspace) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_BACK, 0x7F);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x7F);
}

TEST(input_backspace_alt) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_BACK, 0x7F, LEFT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)0x7F);
}

TEST(input_tab) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_TAB, 0x09);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x09);
}

TEST(input_shift_tab) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_TAB, 0, SHIFT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[Z"));
}

TEST(input_enter) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_RETURN, 0x0D);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x0D);
}

TEST(input_enter_alt) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_RETURN, 0x0D, LEFT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)0x0D);
}

TEST(input_escape) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(VK_ESCAPE, 0x1B);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
}

/*============================================================================
 * Tests - Control Characters
 *============================================================================*/

TEST(input_ctrl_a) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('A', 0x01, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x01);
}

TEST(input_ctrl_c) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('C', 0x03, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x03);
}

TEST(input_ctrl_z) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('Z', 0x1A, LEFT_CTRL_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], (uint8_t)0x1A);
}

TEST(input_alt_ctrl_a) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('A', 0x01, LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)0x01);
}

/*============================================================================
 * Tests - Alt Prefix
 *============================================================================*/

TEST(input_alt_a) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key('A', 'a', LEFT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)'a');
}

TEST(input_altgr_no_esc_prefix) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];
    KEY_EVENT_RECORD k = make_key(0, 0x20AC, LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED);

    int len = wp_input_translate_key(&state, &k, buf);
    ASSERT_EQ(len, 3);
    ASSERT_EQ(buf[0], (uint8_t)0xE2);
    ASSERT_EQ(buf[1], (uint8_t)0x82);
    ASSERT_EQ(buf[2], (uint8_t)0xAC);
}

/*============================================================================
 * Tests - Surrogate Pairs
 *============================================================================*/

TEST(input_surrogate_pair) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k1 = make_key(0, 0xD83D);  /* High surrogate */
    int len1 = wp_input_translate_key(&state, &k1, buf);
    ASSERT_EQ(len1, 0);

    KEY_EVENT_RECORD k2 = make_key(0, 0xDE00);  /* Low surrogate */
    int len2 = wp_input_translate_key(&state, &k2, buf);
    /* U+1F600 -> UTF-8: F0 9F 98 80 */
    ASSERT_EQ(len2, 4);
    ASSERT_EQ(buf[0], (uint8_t)0xF0);
    ASSERT_EQ(buf[1], (uint8_t)0x9F);
    ASSERT_EQ(buf[2], (uint8_t)0x98);
    ASSERT_EQ(buf[3], (uint8_t)0x80);
}

TEST(input_orphan_low_surrogate) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k = make_key(0, 0xDE00);
    ASSERT_EQ(wp_input_translate_key(&state, &k, buf), 0);
}

TEST(input_surrogate_with_alt) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    KEY_EVENT_RECORD k1 = make_key(0, 0xD83D, LEFT_ALT_PRESSED);
    wp_input_translate_key(&state, &k1, buf);

    KEY_EVENT_RECORD k2 = make_key(0, 0xDE00, LEFT_ALT_PRESSED);
    int len = wp_input_translate_key(&state, &k2, buf);
    ASSERT_EQ(len, 5);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)0xF0);
}

/*============================================================================
 * Tests - Mouse (Off)
 *============================================================================*/

TEST(input_mouse_off_no_output) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 10, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    ASSERT_EQ(wp_input_translate_mouse(&state, &m, buf), 0);
}

/*============================================================================
 * Tests - Mouse (Normal Mode, Default Encoding)
 *============================================================================*/

TEST(input_mouse_left_press) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 10, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[0], (uint8_t)0x1B);
    ASSERT_EQ(buf[1], (uint8_t)'[');
    ASSERT_EQ(buf[2], (uint8_t)'M');
    ASSERT_EQ(buf[3], (uint8_t)(32 + 0));   /* Left button */
    ASSERT_EQ(buf[4], (uint8_t)(32 + 6));   /* x=5 -> 1-based 6 */
    ASSERT_EQ(buf[5], (uint8_t)(32 + 11));  /* y=10 -> 1-based 11 */
}

TEST(input_mouse_left_release) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    state.prev_mouse_buttons = FROM_LEFT_1ST_BUTTON_PRESSED;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 10, 0, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 3));  /* Release = code 3 */
}

TEST(input_mouse_right_press) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(0, 0, RIGHTMOST_BUTTON_PRESSED, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 2));  /* Right = code 2 */
}

TEST(input_mouse_middle_press) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(0, 0, FROM_LEFT_2ND_BUTTON_PRESSED, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 1));  /* Middle = code 1 */
}

/*============================================================================
 * Tests - Mouse (X10 Mode)
 *============================================================================*/

TEST(input_mouse_x10_press_only) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_X10;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m1 = make_mouse(0, 0, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    ASSERT_TRUE(wp_input_translate_mouse(&state, &m1, buf) > 0);

    MOUSE_EVENT_RECORD m2 = make_mouse(0, 0, 0, 0);
    ASSERT_EQ(wp_input_translate_mouse(&state, &m2, buf), 0);
}

/*============================================================================
 * Tests - Mouse (SGR Encoding)
 *============================================================================*/

TEST(input_mouse_sgr_press) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    state.mouse_encoding = WP_MOUSE_ENC_SGR;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 10, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[<0;6;11M"));
}

TEST(input_mouse_sgr_release) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    state.mouse_encoding = WP_MOUSE_ENC_SGR;
    state.prev_mouse_buttons = FROM_LEFT_1ST_BUTTON_PRESSED;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 10, 0, 0);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[<0;6;11m"));
}

/*============================================================================
 * Tests - Mouse (Scroll Wheel)
 *============================================================================*/

TEST(input_mouse_wheel_up) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 5, 0, MOUSE_WHEELED);
    m.dwButtonState = (DWORD)(120 << 16);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 64));  /* Wheel up = 64 */
}

TEST(input_mouse_wheel_down) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(5, 5, 0, MOUSE_WHEELED);
    m.dwButtonState = (DWORD)(-120 << 16);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 65));  /* Wheel down = 65 */
}

/*============================================================================
 * Tests - Mouse (Motion Tracking)
 *============================================================================*/

TEST(input_mouse_motion_any_mode) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_ANY;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(10, 20, 0, MOUSE_MOVED);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_TRUE(len > 0);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 35));  /* No button + motion */
}

TEST(input_mouse_motion_normal_mode_ignored) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(10, 20, 0, MOUSE_MOVED);
    ASSERT_EQ(wp_input_translate_mouse(&state, &m, buf), 0);
}

TEST(input_mouse_button_mode_motion_with_held) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_BUTTON;
    state.prev_mouse_buttons = FROM_LEFT_1ST_BUTTON_PRESSED;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(10, 20,
        FROM_LEFT_1ST_BUTTON_PRESSED, MOUSE_MOVED);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_TRUE(len > 0);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 32));  /* Left + motion */
}

TEST(input_mouse_button_mode_motion_without_held) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_BUTTON;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(10, 20, 0, MOUSE_MOVED);
    ASSERT_EQ(wp_input_translate_mouse(&state, &m, buf), 0);
}

/*============================================================================
 * Tests - Mouse (Modifiers)
 *============================================================================*/

TEST(input_mouse_shift_click) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(0, 0,
        FROM_LEFT_1ST_BUTTON_PRESSED, 0, SHIFT_PRESSED);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 4));  /* Left(0) + shift(4) */
}

TEST(input_mouse_ctrl_click) {
    WPInputState state;
    wp_input_init(&state);
    state.mouse_mode = WP_MOUSE_NORMAL;
    uint8_t buf[WP_INPUT_BUF_MAX];

    MOUSE_EVENT_RECORD m = make_mouse(0, 0,
        FROM_LEFT_1ST_BUTTON_PRESSED, 0, LEFT_CTRL_PRESSED);
    int len = wp_input_translate_mouse(&state, &m, buf);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(buf[3], (uint8_t)(32 + 16));  /* Left(0) + ctrl(16) */
}

/*============================================================================
 * Tests - Bracketed Paste
 *============================================================================*/

TEST(input_paste_start_enabled) {
    WPInputState state;
    wp_input_init(&state);
    state.bracketed_paste = true;
    uint8_t buf[WP_INPUT_BUF_MAX];

    int len = wp_input_paste_start(&state, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[200~"));
}

TEST(input_paste_end_enabled) {
    WPInputState state;
    wp_input_init(&state);
    state.bracketed_paste = true;
    uint8_t buf[WP_INPUT_BUF_MAX];

    int len = wp_input_paste_end(&state, buf);
    ASSERT_TRUE(buf_eq(buf, len, "\x1b[201~"));
}

TEST(input_paste_start_disabled) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    ASSERT_EQ(wp_input_paste_start(&state, buf), 0);
}

TEST(input_paste_end_disabled) {
    WPInputState state;
    wp_input_init(&state);
    uint8_t buf[WP_INPUT_BUF_MAX];

    ASSERT_EQ(wp_input_paste_end(&state, buf), 0);
}

TEST(input_paste_null_safe) {
    uint8_t buf[WP_INPUT_BUF_MAX];
    ASSERT_EQ(wp_input_paste_start(NULL, buf), 0);
    ASSERT_EQ(wp_input_paste_end(NULL, buf), 0);
    WPInputState state;
    wp_input_init(&state);
    state.bracketed_paste = true;
    ASSERT_EQ(wp_input_paste_start(&state, NULL), 0);
    ASSERT_EQ(wp_input_paste_end(&state, NULL), 0);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
