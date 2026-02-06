/**
 * @file winpatina_input.cpp
 * @brief Console input handler implementation
 *
 * Translates Win32 console input events into VT escape sequences.
 *
 * Key mapping references:
 *   - xterm ctlseqs: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 *   - VT220 keyboard: DEC standard function key sequences
 *   - mintty/PuTTY modifier encoding conventions
 */

#include "winpatina_input.h"
#include <cstring>  /* memcpy */
#include <cstdio>   /* snprintf */

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Write a simple string to the buffer, return length. */
static int emit(uint8_t* buf, const char* str)
{
    int len = (int)strlen(str);
    memcpy(buf, str, len);
    return len;
}

/**
 * Encode a codepoint as UTF-8 into buf.
 * Returns number of bytes written (1-4), or 0 on error.
 */
static int encode_utf8(uint8_t* buf, uint32_t cp)
{
    if (cp < 0x80) {
        buf[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (cp >> 6));
        buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (cp >> 12));
        buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (uint8_t)(0xF0 | (cp >> 18));
        buf[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/**
 * Compute the xterm modifier parameter for a key event.
 *
 * xterm encodes modifiers as (1 + bitmask):
 *   Shift = 1, Alt = 2, Ctrl = 4
 * So: Shift = 2, Alt = 3, Shift+Alt = 4, Ctrl = 5, etc.
 *
 * Returns 0 if no modifiers are held (meaning: omit the parameter).
 */
static int modifier_param(DWORD control_key_state)
{
    int mod = 0;
    if (control_key_state & SHIFT_PRESSED)
        mod |= 1;
    if (control_key_state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
        mod |= 2;
    if (control_key_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
        mod |= 4;
    return mod ? (1 + mod) : 0;
}

/**
 * Emit a CSI-style function key sequence.
 *
 * For keys like arrows, Home, End that use a final letter:
 *   No modifier:   ESC [ <final>         or  ESC O <final>  (app mode)
 *   With modifier: ESC [ 1 ; <mod> <final>
 *
 * @param buf       Output buffer
 * @param final_ch  Final character (e.g., 'A' for Up)
 * @param mod       Modifier param from modifier_param() (0 = none)
 * @param app_mode  True to use SS3 (ESC O) when unmodified
 * @return Bytes written
 */
static int emit_cursor_key(uint8_t* buf, char final_ch, int mod, bool app_mode)
{
    if (mod == 0) {
        if (app_mode) {
            buf[0] = 0x1B; buf[1] = 'O'; buf[2] = (uint8_t)final_ch;
            return 3;
        }
        buf[0] = 0x1B; buf[1] = '['; buf[2] = (uint8_t)final_ch;
        return 3;
    }
    /* ESC [ 1 ; <mod> <final> */
    return snprintf((char*)buf, WP_INPUT_BUF_MAX,
                    "\x1b[1;%d%c", mod, final_ch);
}

/**
 * Emit a tilde-style function key sequence.
 *
 * For keys like F5-F12, Insert, Delete, PgUp, PgDn:
 *   No modifier:   ESC [ <num> ~
 *   With modifier: ESC [ <num> ; <mod> ~
 *
 * @param buf   Output buffer
 * @param num   Key number (e.g., 15 for F5)
 * @param mod   Modifier param (0 = none)
 * @return Bytes written
 */
static int emit_tilde_key(uint8_t* buf, int num, int mod)
{
    if (mod == 0) {
        return snprintf((char*)buf, WP_INPUT_BUF_MAX,
                        "\x1b[%d~", num);
    }
    return snprintf((char*)buf, WP_INPUT_BUF_MAX,
                    "\x1b[%d;%d~", num, mod);
}

/**
 * Emit an SS3-style function key (F1-F4).
 *
 *   No modifier:   ESC O P/Q/R/S
 *   With modifier: ESC [ 1 ; <mod> P/Q/R/S
 *
 * @param buf       Output buffer
 * @param final_ch  'P', 'Q', 'R', or 'S'
 * @param mod       Modifier param (0 = none)
 * @return Bytes written
 */
static int emit_f1_f4(uint8_t* buf, char final_ch, int mod)
{
    if (mod == 0) {
        buf[0] = 0x1B; buf[1] = 'O'; buf[2] = (uint8_t)final_ch;
        return 3;
    }
    return snprintf((char*)buf, WP_INPUT_BUF_MAX,
                    "\x1b[1;%d%c", mod, final_ch);
}

/*============================================================================
 * Surrogate Pair Handling
 *============================================================================*/

/**
 * Thread-local storage for high surrogate from a previous event.
 * Win32 delivers supplementary-plane characters as two KEY_EVENTs
 * with UChar values forming a UTF-16 surrogate pair.
 */
static thread_local WCHAR pending_high_surrogate = 0;

/*============================================================================
 * Public API - Lifecycle
 *============================================================================*/

void wp_input_init(WPInputState* state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->cursor_key_mode = WP_CURSOR_KEY_NORMAL;
    state->keypad_mode = WP_KEYPAD_NUMERIC;
    state->mouse_mode = WP_MOUSE_OFF;
    state->mouse_encoding = WP_MOUSE_ENC_DEFAULT;
    state->bracketed_paste = false;
    state->prev_mouse_buttons = 0;
}

/*============================================================================
 * Public API - Keyboard Translation
 *============================================================================*/

int wp_input_translate_key(const WPInputState* state,
                           const KEY_EVENT_RECORD* event,
                           uint8_t* buf)
{
    if (state == NULL || event == NULL || buf == NULL) return 0;

    /* Only process key-down events */
    if (!event->bKeyDown) return 0;

    WORD vk = event->wVirtualKeyCode;
    DWORD ctrl = event->dwControlKeyState;
    WCHAR uc = event->uChar.UnicodeChar;
    int mod = modifier_param(ctrl);

    bool app_cursor = (state->cursor_key_mode == WP_CURSOR_KEY_APPLICATION);

    /*
     * Handle UTF-16 surrogate pairs.
     * Win32 delivers supplementary-plane chars as two separate KEY_EVENTs.
     */
    if (uc >= 0xD800 && uc <= 0xDBFF) {
        /* High surrogate - save and wait for low */
        pending_high_surrogate = uc;
        return 0;
    }
    if (uc >= 0xDC00 && uc <= 0xDFFF) {
        /* Low surrogate - combine with pending high */
        if (pending_high_surrogate != 0) {
            uint32_t cp = 0x10000 +
                ((uint32_t)(pending_high_surrogate - 0xD800) << 10) +
                (uint32_t)(uc - 0xDC00);
            pending_high_surrogate = 0;

            /* Alt prefix */
            int pos = 0;
            if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                buf[pos++] = 0x1B;
            }
            pos += encode_utf8(buf + pos, cp);
            return pos;
        }
        pending_high_surrogate = 0;
        return 0;
    }
    pending_high_surrogate = 0;

    /*--------------------------------------------------------------------
     * Virtual key dispatch - special keys
     *--------------------------------------------------------------------*/

    switch (vk) {
        /* Arrow keys */
        case VK_UP:     return emit_cursor_key(buf, 'A', mod, app_cursor);
        case VK_DOWN:   return emit_cursor_key(buf, 'B', mod, app_cursor);
        case VK_RIGHT:  return emit_cursor_key(buf, 'C', mod, app_cursor);
        case VK_LEFT:   return emit_cursor_key(buf, 'D', mod, app_cursor);

        /* Home / End */
        case VK_HOME:   return emit_cursor_key(buf, 'H', mod, app_cursor);
        case VK_END:    return emit_cursor_key(buf, 'F', mod, app_cursor);

        /* Editing keys */
        case VK_INSERT: return emit_tilde_key(buf, 2, mod);
        case VK_DELETE: return emit_tilde_key(buf, 3, mod);
        case VK_PRIOR:  return emit_tilde_key(buf, 5, mod);  /* Page Up */
        case VK_NEXT:   return emit_tilde_key(buf, 6, mod);  /* Page Down */

        /* Function keys F1-F4 (SS3 style) */
        case VK_F1:     return emit_f1_f4(buf, 'P', mod);
        case VK_F2:     return emit_f1_f4(buf, 'Q', mod);
        case VK_F3:     return emit_f1_f4(buf, 'R', mod);
        case VK_F4:     return emit_f1_f4(buf, 'S', mod);

        /* Function keys F5-F12 (tilde style) */
        case VK_F5:     return emit_tilde_key(buf, 15, mod);
        case VK_F6:     return emit_tilde_key(buf, 17, mod);
        case VK_F7:     return emit_tilde_key(buf, 18, mod);
        case VK_F8:     return emit_tilde_key(buf, 19, mod);
        case VK_F9:     return emit_tilde_key(buf, 20, mod);
        case VK_F10:    return emit_tilde_key(buf, 21, mod);
        case VK_F11:    return emit_tilde_key(buf, 23, mod);
        case VK_F12:    return emit_tilde_key(buf, 24, mod);

        /* Backspace */
        case VK_BACK:
            if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                buf[0] = 0x1B; buf[1] = 0x7F;
                return 2;
            }
            buf[0] = 0x7F;
            return 1;

        /* Tab */
        case VK_TAB:
            if (ctrl & SHIFT_PRESSED) {
                return emit(buf, "\x1b[Z");  /* Back-tab (CBT) */
            }
            buf[0] = 0x09;
            return 1;

        /* Enter */
        case VK_RETURN:
            if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                buf[0] = 0x1B; buf[1] = 0x0D;
                return 2;
            }
            buf[0] = 0x0D;
            return 1;

        /* Escape */
        case VK_ESCAPE:
            buf[0] = 0x1B;
            return 1;

        /* Modifier-only keys produce no output */
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:       /* Alt */
        case VK_CAPITAL:    /* Caps Lock */
        case VK_NUMLOCK:
        case VK_SCROLL:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return 0;

        default:
            break;
    }

    /*--------------------------------------------------------------------
     * Character input - UnicodeChar from the event
     *--------------------------------------------------------------------*/

    if (uc == 0) {
        /* No character translation available for this key */
        return 0;
    }

    /*
     * Control characters (Ctrl+letter).
     * Win32 already translates Ctrl+A to 0x01, etc. in UnicodeChar.
     * We just need to handle Alt prefix.
     */
    if (uc < 0x20 || uc == 0x7F) {
        int pos = 0;
        if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
            buf[pos++] = 0x1B;
        }
        buf[pos++] = (uint8_t)uc;
        return pos;
    }

    /*
     * Normal printable characters.
     * Alt+key gets an ESC prefix.
     * The character may be BMP (single WCHAR) - encode as UTF-8.
     */
    {
        int pos = 0;
        if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
            /* Don't send ESC prefix for AltGr characters.
             * AltGr sets both LEFT_CTRL and RIGHT_ALT simultaneously. */
            bool is_altgr = (ctrl & LEFT_CTRL_PRESSED) &&
                            (ctrl & RIGHT_ALT_PRESSED);
            if (!is_altgr) {
                buf[pos++] = 0x1B;
            }
        }
        pos += encode_utf8(buf + pos, (uint32_t)uc);
        return pos;
    }
}

/*============================================================================
 * Public API - Mouse Translation
 *============================================================================*/

int wp_input_translate_mouse(WPInputState* state,
                             const MOUSE_EVENT_RECORD* event,
                             uint8_t* buf)
{
    if (state == NULL || event == NULL || buf == NULL) return 0;
    if (state->mouse_mode == WP_MOUSE_OFF) return 0;

    /* 1-based coordinates for VT mouse reporting */
    int x = event->dwMousePosition.X + 1;
    int y = event->dwMousePosition.Y + 1;

    DWORD buttons = event->dwButtonState;
    DWORD prev = state->prev_mouse_buttons;
    DWORD event_flags = event->dwEventFlags;
    DWORD ctrl = event->dwControlKeyState;

    /* Modifier bits for the encoded button value */
    int mod_bits = 0;
    if (ctrl & SHIFT_PRESSED) mod_bits |= 4;
    if (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) mod_bits |= 8;
    if (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) mod_bits |= 16;

    /*
     * Determine what happened.
     * Win32 gives us the full button state, not individual press/release.
     * We compare with previous state to determine transitions.
     */

    bool is_motion = (event_flags & MOUSE_MOVED) != 0;
    bool is_wheel = (event_flags & MOUSE_WHEELED) != 0;

    /* Calculate button that changed */
    DWORD changed = buttons ^ prev;
    state->prev_mouse_buttons = buttons;

    /* Helper: encode and emit a mouse event */
    auto emit_mouse = [&](int button_code, bool release) -> int {
        int cb = button_code | mod_bits;

        if (state->mouse_encoding == WP_MOUSE_ENC_SGR) {
            /* SGR format: ESC [ < Cb ; Cx ; Cy M (press) or m (release) */
            return snprintf((char*)buf, WP_INPUT_BUF_MAX,
                            "\x1b[<%d;%d;%d%c",
                            cb, x, y, release ? 'm' : 'M');
        }

        /* Default X10 format: ESC [ M Cb Cx Cy */
        /* Coordinates capped at 223 (+ 32 = 255, max single byte) */
        if (x > 223) x = 223;
        if (y > 223) y = 223;
        buf[0] = 0x1B;
        buf[1] = '[';
        buf[2] = 'M';
        buf[3] = (uint8_t)(32 + cb);
        buf[4] = (uint8_t)(32 + x);
        buf[5] = (uint8_t)(32 + y);
        return 6;
    };

    /* Scroll wheel */
    if (is_wheel) {
        if (state->mouse_mode == WP_MOUSE_X10 ||
            state->mouse_mode == WP_MOUSE_NORMAL ||
            state->mouse_mode == WP_MOUSE_BUTTON ||
            state->mouse_mode == WP_MOUSE_ANY) {
            /* High word of dwButtonState contains wheel delta */
            short delta = (short)HIWORD(buttons);
            int button_code = (delta > 0) ? 64 : 65;  /* 64=up, 65=down */
            return emit_mouse(button_code, false);
        }
        return 0;
    }

    /* Motion events */
    if (is_motion && changed == 0) {
        /* Pure motion, no button change */
        if (state->mouse_mode == WP_MOUSE_ANY) {
            /* Report all motion */
            int button_code = 35;  /* No button + motion flag (32 + 3) */
            if (buttons & FROM_LEFT_1ST_BUTTON_PRESSED) button_code = 32;
            else if (buttons & RIGHTMOST_BUTTON_PRESSED) button_code = 34;
            else if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED) button_code = 33;
            return emit_mouse(button_code, false);
        }
        if (state->mouse_mode == WP_MOUSE_BUTTON && buttons != 0) {
            /* Report motion while button held */
            int button_code = 32;  /* Left + motion */
            if (buttons & RIGHTMOST_BUTTON_PRESSED) button_code = 34;
            else if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED) button_code = 33;
            return emit_mouse(button_code, false);
        }
        return 0;
    }

    /* Button press/release */
    if (changed != 0) {
        /* Find which button changed */
        int button_code = 0;
        bool is_release = false;

        if (changed & FROM_LEFT_1ST_BUTTON_PRESSED) {
            button_code = 0;  /* Left */
            is_release = !(buttons & FROM_LEFT_1ST_BUTTON_PRESSED);
        } else if (changed & RIGHTMOST_BUTTON_PRESSED) {
            button_code = 2;  /* Right */
            is_release = !(buttons & RIGHTMOST_BUTTON_PRESSED);
        } else if (changed & FROM_LEFT_2ND_BUTTON_PRESSED) {
            button_code = 1;  /* Middle */
            is_release = !(buttons & FROM_LEFT_2ND_BUTTON_PRESSED);
        } else {
            return 0;  /* Other buttons not mapped */
        }

        /* X10 mode: only reports presses */
        if (state->mouse_mode == WP_MOUSE_X10 && is_release) {
            return 0;
        }

        if (is_release && state->mouse_encoding == WP_MOUSE_ENC_DEFAULT) {
            /* Default encoding uses button code 3 for all releases */
            button_code = 3;
        }

        /* Add motion flag if mouse also moved */
        if (is_motion) {
            button_code |= 32;
        }

        return emit_mouse(button_code, is_release);
    }

    return 0;
}

/*============================================================================
 * Public API - Bracketed Paste
 *============================================================================*/

int wp_input_paste_start(const WPInputState* state, uint8_t* buf)
{
    if (state == NULL || buf == NULL) return 0;
    if (!state->bracketed_paste) return 0;
    return emit(buf, "\x1b[200~");
}

int wp_input_paste_end(const WPInputState* state, uint8_t* buf)
{
    if (state == NULL || buf == NULL) return 0;
    if (!state->bracketed_paste) return 0;
    return emit(buf, "\x1b[201~");
}
