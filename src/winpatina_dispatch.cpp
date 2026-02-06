/**
 * @file winpatina_dispatch.cpp
 * @brief VT sequence dispatch handler implementation
 *
 * Translates VT parser callbacks into screen buffer and colour
 * operations. This is the central interpreter that gives meaning
 * to the escape sequences the parser recognises.
 *
 * Supported sequences:
 *
 * C0 Controls (on_execute):
 *   BEL (0x07)  - Bell (ignored)
 *   BS  (0x08)  - Backspace: cursor left 1
 *   HT  (0x09)  - Horizontal tab: advance to next tab stop (every 8)
 *   LF  (0x0A)  - Line feed: cursor down, scroll if at bottom
 *   VT  (0x0B)  - Vertical tab: same as LF
 *   FF  (0x0C)  - Form feed: same as LF
 *   CR  (0x0D)  - Carriage return: cursor to column 0
 *
 * CSI Sequences (on_csi):
 *   m   (SGR)     - Select Graphic Rendition
 *   H/f (CUP/HVP) - Cursor Position
 *   A   (CUU)     - Cursor Up
 *   B   (CUD)     - Cursor Down
 *   C   (CUF)     - Cursor Forward
 *   D   (CUB)     - Cursor Back
 *   E   (CNL)     - Cursor Next Line
 *   F   (CPL)     - Cursor Previous Line
 *   G   (CHA)     - Cursor Horizontal Absolute
 *   d   (VPA)     - Vertical Position Absolute
 *   J   (ED)      - Erase in Display
 *   K   (EL)      - Erase in Line
 *   X   (ECH)     - Erase Characters
 *   S   (SU)      - Scroll Up
 *   T   (SD)      - Scroll Down
 *   L   (IL)      - Insert Lines
 *   M   (DL)      - Delete Lines
 *   @   (ICH)     - Insert Characters
 *   P   (DCH)     - Delete Characters
 *   r   (DECSTBM) - Set Scrolling Region
 *   s   (SCP)     - Save Cursor Position
 *   u   (RCP)     - Restore Cursor Position
 *   b   (REP)     - Repeat Preceding Graphic Character
 *   h   (SM/DECSET) - Set Mode
 *   l   (RM/DECRST) - Reset Mode
 *   n   (DSR)     - Device Status Report
 *
 * ESC Sequences (on_esc):
 *   7   (DECSC)   - Save Cursor
 *   8   (DECRC)   - Restore Cursor
 *   M   (RI)      - Reverse Index
 *   D   (IND)     - Index (line feed)
 *   E   (NEL)     - Next Line (CR + LF)
 *   c   (RIS)     - Full Reset
 */

#include "winpatina_dispatch.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Forward Declarations (internal callbacks)
 *============================================================================*/

static void dispatch_print(void* user_data, uint32_t codepoint);
static void dispatch_execute(void* user_data, uint8_t byte);
static void dispatch_csi(void* user_data, const WPVTParser* parser,
                         char final_byte);
static void dispatch_esc(void* user_data, const WPVTParser* parser,
                         char final_byte);
static void dispatch_osc(void* user_data, const WPVTParser* parser);

/*============================================================================
 * SGR (Select Graphic Rendition) Handler
 *============================================================================*/

/**
 * Process a single SGR parameter and update the SGR state accordingly.
 * Called iteratively for each parameter in a CSI m sequence.
 *
 * @param state  Dispatch state
 * @param parser Parser (for reading sub-parameters in extended colour)
 * @param pi     Pointer to the current parameter index (may be advanced
 *               for extended colour sequences like 38;5;N or 38;2;R;G;B)
 */
static void handle_sgr_param(WPDispatchState* state,
                             const WPVTParser* parser, int* pi)
{
    int p = wp_vt_get_param(parser, *pi, 0);

    switch (p) {
    case 0: /* Reset */
        wp_sgr_reset(&state->sgr, state->default_attrs);
        break;

    case 1: /* Bold */
        state->sgr.bold = true;
        break;

    case 2: /* Dim */
        state->sgr.dim = true;
        break;

    case 4: /* Underline */
        state->sgr.underline = true;
        break;

    case 7: /* Reverse video */
        state->sgr.reverse = true;
        break;

    case 22: /* Normal intensity (not bold, not dim) */
        state->sgr.bold = false;
        state->sgr.dim = false;
        break;

    case 24: /* Not underlined */
        state->sgr.underline = false;
        break;

    case 27: /* Not reversed */
        state->sgr.reverse = false;
        break;

    /* Foreground colours 30-37 (standard ANSI 8) */
    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37:
        state->sgr.fg_index = p - 30;
        break;

    case 38: { /* Extended foreground */
        int mode = wp_vt_get_param(parser, *pi + 1, 0);
        if (mode == 5) {
            /* 256-colour: 38;5;N */
            int idx = wp_vt_get_param(parser, *pi + 2, 0);
            if (idx < 16) {
                state->sgr.fg_index = idx;
            } else {
                state->sgr.fg_index = wp_colour_256_to_16(idx);
            }
            *pi += 2;
        } else if (mode == 2) {
            /* RGB: 38;2;R;G;B */
            int r = wp_vt_get_param(parser, *pi + 2, 0);
            int g = wp_vt_get_param(parser, *pi + 3, 0);
            int b = wp_vt_get_param(parser, *pi + 4, 0);
            state->sgr.fg_index = wp_colour_rgb_to_16(r, g, b);
            *pi += 4;
        }
        break;
    }

    case 39: /* Default foreground */
        state->sgr.fg_index = -1;
        break;

    /* Background colours 40-47 (standard ANSI 8) */
    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47:
        state->sgr.bg_index = p - 40;
        break;

    case 48: { /* Extended background */
        int mode = wp_vt_get_param(parser, *pi + 1, 0);
        if (mode == 5) {
            /* 256-colour: 48;5;N */
            int idx = wp_vt_get_param(parser, *pi + 2, 0);
            if (idx < 16) {
                state->sgr.bg_index = idx;
            } else {
                state->sgr.bg_index = wp_colour_256_to_16(idx);
            }
            *pi += 2;
        } else if (mode == 2) {
            /* RGB: 48;2;R;G;B */
            int r = wp_vt_get_param(parser, *pi + 2, 0);
            int g = wp_vt_get_param(parser, *pi + 3, 0);
            int b = wp_vt_get_param(parser, *pi + 4, 0);
            state->sgr.bg_index = wp_colour_rgb_to_16(r, g, b);
            *pi += 4;
        }
        break;
    }

    case 49: /* Default background */
        state->sgr.bg_index = -1;
        break;

    /* Bright foreground colours 90-97 */
    case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97:
        state->sgr.fg_index = (p - 90) + 8;
        break;

    /* Bright background colours 100-107 */
    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107:
        state->sgr.bg_index = (p - 100) + 8;
        break;

    default:
        /* Unrecognised SGR parameter -- ignore */
        break;
    }
}

/**
 * Process a complete SGR sequence (CSI ... m).
 * Iterates through all parameters and updates the SGR state,
 * then converts the new state to Win32 attributes.
 */
static void handle_sgr(WPDispatchState* state, const WPVTParser* parser)
{
    int count = parser->param_count;

    if (count == 0) {
        /* CSI m with no params is equivalent to CSI 0 m (reset) */
        wp_sgr_reset(&state->sgr, state->default_attrs);
    } else {
        for (int i = 0; i < count; i++) {
            handle_sgr_param(state, parser, &i);
        }
    }

    /* Update the screen buffer's current attributes */
    state->screen->current_attrs =
        wp_sgr_to_attrs(&state->sgr, state->default_attrs, state->has_lvb);
}

/*============================================================================
 * Private Mode Handling (DECSET / DECRST)
 *============================================================================*/

/**
 * Handle CSI ? ... h (DECSET) and CSI ? ... l (DECRST).
 *
 * @param state   Dispatch state
 * @param parser  Parser (for reading parameters)
 * @param enable  true for DECSET (h), false for DECRST (l)
 */
static void handle_private_mode(WPDispatchState* state,
                                const WPVTParser* parser, bool enable)
{
    for (int i = 0; i < parser->param_count; i++) {
        int mode = wp_vt_get_param(parser, i, 0);

        switch (mode) {
        case 25: /* DECTCEM - cursor visibility */
            state->screen->cursor.visible = enable;
            break;

        case 1049: /* Alternate screen buffer */
            if (enable) {
                wp_screen_enter_alternate(state->screen);
            } else {
                wp_screen_leave_alternate(state->screen);
            }
            break;

        case 47:  /* Alternate screen (older form, no save/restore) */
        case 1047:
            if (enable) {
                wp_screen_enter_alternate(state->screen);
            } else {
                wp_screen_leave_alternate(state->screen);
            }
            break;

        default:
            /* Unrecognised private mode -- ignore */
            break;
        }
    }
}

/*============================================================================
 * Device Status Report (DSR)
 *============================================================================*/

/**
 * Handle CSI n (Device Status Report).
 * Sends a response back via the write-back callback if available.
 */
static void handle_dsr(WPDispatchState* state, const WPVTParser* parser)
{
    if (!state->on_write_back) {
        return;
    }

    int param = wp_vt_get_param(parser, 0, 0);

    switch (param) {
    case 5: {
        /* Status report: respond "OK" */
        const uint8_t response[] = "\x1b[0n";
        state->on_write_back(state->write_back_data,
                             response, sizeof(response) - 1);
        break;
    }
    case 6: {
        /* Cursor position report: respond ESC [ row ; col R */
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dR",
                           state->screen->cursor.y + 1,
                           state->screen->cursor.x + 1);
        if (len > 0) {
            state->on_write_back(state->write_back_data,
                                 (const uint8_t*)buf, (size_t)len);
        }
        break;
    }
    default:
        break;
    }
}

/*============================================================================
 * Linefeed Helper
 *============================================================================*/

/**
 * Perform a line feed: move cursor down one row. If the cursor is
 * at the bottom of the scroll region, scroll up instead.
 */
static void do_linefeed(WPScreenBuffer* screen)
{
    if (screen->cursor.y == screen->scroll_bottom) {
        /* At bottom of scroll region -- scroll up */
        wp_screen_scroll(screen, 1);
    } else if (screen->cursor.y < screen->height - 1) {
        screen->cursor.y++;
    }
    screen->cursor.pending_wrap = false;
}

/**
 * Perform a reverse index: move cursor up one row. If the cursor is
 * at the top of the scroll region, scroll down instead.
 */
static void do_reverse_index(WPScreenBuffer* screen)
{
    if (screen->cursor.y == screen->scroll_top) {
        /* At top of scroll region -- scroll down */
        wp_screen_scroll(screen, -1);
    } else if (screen->cursor.y > 0) {
        screen->cursor.y--;
    }
    screen->cursor.pending_wrap = false;
}

/*============================================================================
 * Parser Callbacks
 *============================================================================*/

static void dispatch_print(void* user_data, uint32_t codepoint)
{
    WPDispatchState* state = (WPDispatchState*)user_data;

    wp_screen_put_char(state->screen, codepoint);
    state->last_print = codepoint;
}

static void dispatch_execute(void* user_data, uint8_t byte)
{
    WPDispatchState* state = (WPDispatchState*)user_data;
    WPScreenBuffer* screen = state->screen;

    switch (byte) {
    case 0x07: /* BEL - bell (ignored) */
        break;

    case 0x08: /* BS - backspace */
        if (screen->cursor.x > 0) {
            screen->cursor.x--;
        }
        screen->cursor.pending_wrap = false;
        break;

    case 0x09: { /* HT - horizontal tab */
        int next_tab = (screen->cursor.x / 8 + 1) * 8;
        if (next_tab >= screen->width) {
            next_tab = screen->width - 1;
        }
        screen->cursor.x = next_tab;
        screen->cursor.pending_wrap = false;
        break;
    }

    case 0x0A: /* LF - line feed */
    case 0x0B: /* VT - vertical tab (same as LF) */
    case 0x0C: /* FF - form feed (same as LF) */
        do_linefeed(screen);
        break;

    case 0x0D: /* CR - carriage return */
        screen->cursor.x = 0;
        screen->cursor.pending_wrap = false;
        break;

    default:
        /* Other C0 controls are ignored */
        break;
    }
}

static void dispatch_csi(void* user_data, const WPVTParser* parser,
                         char final_byte)
{
    WPDispatchState* state = (WPDispatchState*)user_data;
    WPScreenBuffer* screen = state->screen;
    bool is_private = wp_vt_has_intermediate(parser, '?');

    switch (final_byte) {
    case 'm': /* SGR - Select Graphic Rendition */
        handle_sgr(state, parser);
        break;

    case 'H': /* CUP - Cursor Position */
    case 'f': { /* HVP - Horizontal and Vertical Position */
        int row = wp_vt_get_param(parser, 0, 1);
        int col = wp_vt_get_param(parser, 1, 1);
        wp_screen_set_cursor(screen, col - 1, row - 1);
        break;
    }

    case 'A': { /* CUU - Cursor Up */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_move_cursor(screen, 0, -n);
        break;
    }

    case 'B': { /* CUD - Cursor Down */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_move_cursor(screen, 0, n);
        break;
    }

    case 'C': { /* CUF - Cursor Forward */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_move_cursor(screen, n, 0);
        break;
    }

    case 'D': { /* CUB - Cursor Back */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_move_cursor(screen, -n, 0);
        break;
    }

    case 'E': { /* CNL - Cursor Next Line */
        int n = wp_vt_get_param(parser, 0, 1);
        screen->cursor.x = 0;
        wp_screen_move_cursor(screen, 0, n);
        break;
    }

    case 'F': { /* CPL - Cursor Previous Line */
        int n = wp_vt_get_param(parser, 0, 1);
        screen->cursor.x = 0;
        wp_screen_move_cursor(screen, 0, -n);
        break;
    }

    case 'G': /* CHA - Cursor Horizontal Absolute */
    case '`': { /* HPA - same as CHA */
        int col = wp_vt_get_param(parser, 0, 1);
        wp_screen_set_cursor(screen, col - 1, screen->cursor.y);
        break;
    }

    case 'd': { /* VPA - Vertical Position Absolute */
        int row = wp_vt_get_param(parser, 0, 1);
        wp_screen_set_cursor(screen, screen->cursor.x, row - 1);
        break;
    }

    case 'J': { /* ED - Erase in Display */
        int mode = wp_vt_get_param(parser, 0, 0);
        wp_screen_erase_display(screen, mode);
        break;
    }

    case 'K': { /* EL - Erase in Line */
        int mode = wp_vt_get_param(parser, 0, 0);
        wp_screen_erase_line(screen, mode);
        break;
    }

    case 'X': { /* ECH - Erase Characters */
        int n = wp_vt_get_param(parser, 0, 1);
        int cx = screen->cursor.x;
        int cy = screen->cursor.y;
        for (int i = 0; i < n && (cx + i) < screen->width; i++) {
            WPScreenCell* cell = wp_screen_cell_at(screen, cx + i, cy);
            if (cell) {
                cell->codepoint = ' ';
                cell->attributes = screen->current_attrs;
                cell->wide_trail = false;
            }
        }
        wp_screen_mark_dirty(screen, cy);
        break;
    }

    case 'S': { /* SU - Scroll Up */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_scroll(screen, n);
        break;
    }

    case 'T': { /* SD - Scroll Down */
        if (!is_private) {
            int n = wp_vt_get_param(parser, 0, 1);
            wp_screen_scroll(screen, -n);
        }
        break;
    }

    case 'L': { /* IL - Insert Lines */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_insert_lines(screen, n);
        break;
    }

    case 'M': { /* DL - Delete Lines */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_delete_lines(screen, n);
        break;
    }

    case '@': { /* ICH - Insert Characters */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_insert_chars(screen, n);
        break;
    }

    case 'P': { /* DCH - Delete Characters */
        int n = wp_vt_get_param(parser, 0, 1);
        wp_screen_delete_chars(screen, n);
        break;
    }

    case 'r': { /* DECSTBM - Set Scrolling Region */
        int top = wp_vt_get_param(parser, 0, 1);
        int bottom = wp_vt_get_param(parser, 1, screen->height);
        wp_screen_set_scroll_region(screen, top - 1, bottom - 1);
        /* DECSTBM also homes the cursor */
        wp_screen_set_cursor(screen, 0, 0);
        break;
    }

    case 's': /* SCP - Save Cursor Position */
        wp_screen_save_cursor(screen);
        break;

    case 'u': /* RCP - Restore Cursor Position */
        wp_screen_restore_cursor(screen);
        break;

    case 'b': { /* REP - Repeat Preceding Graphic Character */
        int n = wp_vt_get_param(parser, 0, 1);
        if (state->last_print != 0) {
            for (int i = 0; i < n; i++) {
                wp_screen_put_char(screen, state->last_print);
            }
        }
        break;
    }

    case 'h': /* SM/DECSET - Set Mode */
        if (is_private) {
            handle_private_mode(state, parser, true);
        }
        break;

    case 'l': /* RM/DECRST - Reset Mode */
        if (is_private) {
            handle_private_mode(state, parser, false);
        }
        break;

    case 'n': /* DSR - Device Status Report */
        handle_dsr(state, parser);
        break;

    default:
        /* Unrecognised CSI sequence -- ignore */
        break;
    }
}

static void dispatch_esc(void* user_data, const WPVTParser* parser,
                         char final_byte)
{
    WPDispatchState* state = (WPDispatchState*)user_data;
    WPScreenBuffer* screen = state->screen;

    (void)parser;  /* Intermediates not used for the sequences we handle */

    switch (final_byte) {
    case '7': /* DECSC - Save Cursor */
        wp_screen_save_cursor(screen);
        break;

    case '8': /* DECRC - Restore Cursor */
        wp_screen_restore_cursor(screen);
        break;

    case 'M': /* RI - Reverse Index */
        do_reverse_index(screen);
        break;

    case 'D': /* IND - Index (same as LF) */
        do_linefeed(screen);
        break;

    case 'E': /* NEL - Next Line (CR + LF) */
        screen->cursor.x = 0;
        screen->cursor.pending_wrap = false;
        do_linefeed(screen);
        break;

    case 'c': /* RIS - Full Reset */
        wp_sgr_reset(&state->sgr, state->default_attrs);
        screen->current_attrs = state->default_attrs;
        wp_screen_set_scroll_region(screen, 0, screen->height - 1);
        wp_screen_set_cursor(screen, 0, 0);
        screen->cursor.visible = true;
        wp_screen_erase_display(screen, 2);
        state->last_print = 0;
        break;

    default:
        /* Unrecognised ESC sequence -- ignore */
        break;
    }
}

static void dispatch_osc(void* user_data, const WPVTParser* parser)
{
    (void)user_data;
    (void)parser;

    /*
     * OSC sequences (window title, etc.) are recognised but not
     * acted upon -- the Win32 console title is managed separately.
     * This could be extended later to set the console title via
     * SetConsoleTitleW for OSC 0 and OSC 2.
     */
}

/*============================================================================
 * Public API
 *============================================================================*/

void wp_dispatch_init(WPDispatchState* state, WPScreenBuffer* screen,
                      WORD default_attrs, bool has_lvb)
{
    memset(state, 0, sizeof(*state));
    state->screen = screen;
    state->default_attrs = default_attrs;
    state->has_lvb = has_lvb;
    state->last_print = 0;
    state->on_write_back = NULL;
    state->write_back_data = NULL;

    wp_sgr_init(&state->sgr, default_attrs);
}

void wp_dispatch_attach(WPDispatchState* state, WPVTParser* parser)
{
    parser->user_data   = state;
    parser->on_print    = dispatch_print;
    parser->on_execute  = dispatch_execute;
    parser->on_csi      = dispatch_csi;
    parser->on_esc      = dispatch_esc;
    parser->on_osc      = dispatch_osc;
    parser->on_dcs      = NULL;  /* DCS not handled yet */
}

void wp_dispatch_set_write_back(WPDispatchState* state,
                                wp_dispatch_write_back_fn fn,
                                void* wb_data)
{
    state->on_write_back = fn;
    state->write_back_data = wb_data;
}
