/**
 * @file winpatina_vt_parser.cpp
 * @brief VT escape sequence parser implementation
 *
 * State machine for parsing VT/ANSI escape sequences based on the
 * Paul Williams VT parser model. Processes a byte stream and invokes
 * callbacks when complete sequences are recognised.
 *
 * Design notes:
 *
 * - 8-bit C1 controls (0x80-0x9F) are NOT interpreted as control codes.
 *   In UTF-8 mode these bytes are part of multibyte sequences. Only the
 *   7-bit ESC-based equivalents (e.g., ESC [ for CSI) are used.
 *   Exception: 0x9C (ST) is recognised in string-collecting states
 *   (OSC, DCS) as some terminals emit it.
 *
 * - "Anywhere" transitions (ESC, CAN, SUB) are handled before the
 *   per-state switch. ESC always enters ESCAPE state; CAN/SUB always
 *   cancel the current sequence and return to GROUND.
 *
 * - C0 controls (BEL, BS, HT, LF, VT, FF, CR) are executed even in
 *   the middle of escape sequences, matching real terminal behaviour.
 *
 * - UTF-8 decoding is performed in GROUND state only. Overlong
 *   encodings, surrogates, and out-of-range codepoints are replaced
 *   with U+FFFD.
 */

#include "winpatina_vt_parser.h"
#include <string.h>

/*============================================================================
 * Internal Helpers - Parameter Accumulation
 *============================================================================*/

/** Clear parameter accumulators, intermediates, and sub-parameter flag */
static void clear_params(WPVTParser* parser)
{
    parser->param_count = 0;
    parser->current_param = 0;
    parser->param_has_value = false;
    parser->has_subparams = false;
    parser->intermediate_count = 0;
}

/**
 * Push the current parameter value and start a new one.
 *
 * If no digits were collected for this parameter (param_has_value is false),
 * -1 is stored to indicate an omitted/default parameter. This lets
 * wp_vt_get_param() return the caller's default for missing values.
 */
static void push_param(WPVTParser* parser)
{
    if (parser->param_count < WP_MAX_PARAMS) {
        parser->params[parser->param_count] =
            parser->param_has_value ? parser->current_param : -1;
        parser->param_count++;
    }
    parser->current_param = 0;
    parser->param_has_value = false;
}

/** Accumulate a digit (0x30-0x39) into the current parameter */
static void collect_digit(WPVTParser* parser, uint8_t byte)
{
    parser->current_param = parser->current_param * 10 + (byte - '0');
    parser->param_has_value = true;

    /* Cap to prevent integer overflow on malformed input */
    if (parser->current_param > 65535) {
        parser->current_param = 65535;
    }
}

/** Collect an intermediate byte (0x20-0x2F) or private marker (0x3C-0x3F) */
static void collect_intermediate(WPVTParser* parser, uint8_t byte)
{
    if (parser->intermediate_count < WP_MAX_INTERMEDIATES) {
        parser->intermediates[parser->intermediate_count++] = (char)byte;
    }
}

/*============================================================================
 * Internal Helpers - String Accumulation (OSC, DCS)
 *============================================================================*/

/** Append a byte to the string buffer */
static void collect_string(WPVTParser* parser, uint8_t byte)
{
    if (parser->string_len < WP_MAX_STRING_LEN - 1) {
        parser->string_buffer[parser->string_len++] = (char)byte;
        parser->string_buffer[parser->string_len] = '\0';
    }
}

/** Clear the string buffer */
static void clear_string(WPVTParser* parser)
{
    parser->string_len = 0;
    parser->string_buffer[0] = '\0';
}

/*============================================================================
 * Internal Helpers - Callback Dispatch
 *============================================================================*/

static void do_execute(WPVTParser* parser, uint8_t byte)
{
    if (parser->on_execute) {
        parser->on_execute(parser->user_data, byte);
    }
}

static void do_print(WPVTParser* parser, uint32_t codepoint)
{
    if (parser->on_print) {
        parser->on_print(parser->user_data, codepoint);
    }
}

/**
 * Dispatch a CSI sequence.
 *
 * Pushes the final parameter (if any) before invoking the callback,
 * so the callback sees the complete parameter list.
 */
static void do_csi_dispatch(WPVTParser* parser, char final_byte)
{
    /* Push the trailing parameter if we were accumulating one,
     * or if we've already pushed at least one param (trailing ';') */
    if (parser->param_has_value || parser->param_count > 0) {
        push_param(parser);
    }

    if (parser->on_csi) {
        parser->on_csi(parser->user_data, parser, final_byte);
    }
}

static void do_esc_dispatch(WPVTParser* parser, char final_byte)
{
    if (parser->on_esc) {
        parser->on_esc(parser->user_data, parser, final_byte);
    }
}

static void do_osc_dispatch(WPVTParser* parser)
{
    if (parser->on_osc) {
        parser->on_osc(parser->user_data, parser);
    }
}

static void do_dcs_dispatch(WPVTParser* parser)
{
    if (parser->on_dcs) {
        parser->on_dcs(parser->user_data, parser, parser->dcs_final);
    }
}

/*============================================================================
 * Internal Helpers - UTF-8 Decoding
 *============================================================================*/

/** Reset the UTF-8 accumulator */
static void utf8_reset(WPVTParser* parser)
{
    parser->utf8_expected = 0;
    parser->utf8_received = 0;
}

/**
 * Decode the accumulated UTF-8 bytes into a codepoint and emit via on_print.
 *
 * Validates the result: overlong encodings, surrogates (U+D800-U+DFFF),
 * and values beyond U+10FFFF are replaced with U+FFFD.
 */
static void utf8_complete(WPVTParser* parser)
{
    uint32_t cp = 0;

    switch (parser->utf8_expected) {
    case 2:
        cp = ((uint32_t)(parser->utf8_buffer[0] & 0x1F) << 6)
           | ((uint32_t)(parser->utf8_buffer[1] & 0x3F));
        if (cp < 0x80) {
            cp = 0xFFFD;
        }
        break;

    case 3:
        cp = ((uint32_t)(parser->utf8_buffer[0] & 0x0F) << 12)
           | ((uint32_t)(parser->utf8_buffer[1] & 0x3F) << 6)
           | ((uint32_t)(parser->utf8_buffer[2] & 0x3F));
        if (cp < 0x800) {
            cp = 0xFFFD;
        }
        break;

    case 4:
        cp = ((uint32_t)(parser->utf8_buffer[0] & 0x07) << 18)
           | ((uint32_t)(parser->utf8_buffer[1] & 0x3F) << 12)
           | ((uint32_t)(parser->utf8_buffer[2] & 0x3F) << 6)
           | ((uint32_t)(parser->utf8_buffer[3] & 0x3F));
        if (cp < 0x10000) {
            cp = 0xFFFD;
        }
        break;

    default:
        cp = 0xFFFD;
        break;
    }

    /* Reject surrogates */
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        cp = 0xFFFD;
    }

    /* Reject values beyond Unicode range */
    if (cp > 0x10FFFF) {
        cp = 0xFFFD;
    }

    do_print(parser, cp);
    utf8_reset(parser);
}

/*============================================================================
 * Internal Helpers - C0 Control Detection
 *============================================================================*/

/**
 * Check whether a C0 byte should be executed.
 *
 * These controls are meaningful to terminal display and are executed
 * even in the middle of escape sequence parsing. Other C0 controls
 * (NUL, SOH, STX, etc.) are silently ignored.
 */
static bool is_c0_executable(uint8_t byte)
{
    switch (byte) {
    case 0x07: /* BEL - bell */
    case 0x08: /* BS  - backspace */
    case 0x09: /* HT  - horizontal tab */
    case 0x0A: /* LF  - line feed */
    case 0x0B: /* VT  - vertical tab (treated as LF) */
    case 0x0C: /* FF  - form feed (treated as LF) */
    case 0x0D: /* CR  - carriage return */
        return true;
    default:
        return false;
    }
}

/*============================================================================
 * Public API - Initialisation
 *============================================================================*/

void wp_vt_parser_init(WPVTParser* parser, void* user_data)
{
    memset(parser, 0, sizeof(*parser));
    parser->state = WP_VT_GROUND;
    parser->pre_escape_state = WP_VT_GROUND;
    parser->user_data = user_data;
}

void wp_vt_parser_reset(WPVTParser* parser)
{
    /* Preserve callbacks and user_data */
    void* ud = parser->user_data;
    wp_vt_on_print_fn   fn_print   = parser->on_print;
    wp_vt_on_execute_fn  fn_execute = parser->on_execute;
    wp_vt_on_csi_fn      fn_csi     = parser->on_csi;
    wp_vt_on_esc_fn      fn_esc     = parser->on_esc;
    wp_vt_on_osc_fn      fn_osc     = parser->on_osc;
    wp_vt_on_dcs_fn      fn_dcs     = parser->on_dcs;

    memset(parser, 0, sizeof(*parser));

    parser->state = WP_VT_GROUND;
    parser->pre_escape_state = WP_VT_GROUND;
    parser->user_data = ud;
    parser->on_print   = fn_print;
    parser->on_execute = fn_execute;
    parser->on_csi     = fn_csi;
    parser->on_esc     = fn_esc;
    parser->on_osc     = fn_osc;
    parser->on_dcs     = fn_dcs;
}

/*============================================================================
 * Public API - Utility Functions
 *============================================================================*/

int wp_vt_get_param(const WPVTParser* parser, int index, int default_value)
{
    if (index < 0 || index >= parser->param_count) {
        return default_value;
    }

    /* -1 indicates an omitted parameter (e.g., ESC [ ; 5 H -> param 0 is -1) */
    if (parser->params[index] < 0) {
        return default_value;
    }

    return parser->params[index];
}

bool wp_vt_has_intermediate(const WPVTParser* parser, char ch)
{
    for (int i = 0; i < parser->intermediate_count; i++) {
        if (parser->intermediates[i] == ch) {
            return true;
        }
    }
    return false;
}

/*============================================================================
 * Public API - Feed
 *
 * This is the core of the parser. Each byte is processed through the
 * state machine, with "anywhere" transitions checked first.
 *============================================================================*/

void wp_vt_parser_feed(WPVTParser* parser, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        /*--------------------------------------------------------------
         * "Anywhere" transitions
         *
         * These override the current state unconditionally.
         *--------------------------------------------------------------*/

        /* ESC (0x1B): Enter ESCAPE state, cancelling any in-progress sequence */
        if (byte == 0x1B) {
            utf8_reset(parser);
            parser->pre_escape_state = parser->state;
            clear_params(parser);
            parser->state = WP_VT_ESCAPE;
            continue;
        }

        /* CAN (0x18), SUB (0x1A): Cancel and return to GROUND */
        if (byte == 0x18 || byte == 0x1A) {
            utf8_reset(parser);
            do_execute(parser, byte);
            parser->state = WP_VT_GROUND;
            continue;
        }

        /*--------------------------------------------------------------
         * Per-state processing
         *--------------------------------------------------------------*/

        switch (parser->state) {

        /*=== GROUND: Normal text processing ===*/
        case WP_VT_GROUND:
            /*
             * If we're mid UTF-8 sequence, the next byte must be a
             * continuation byte. If not, emit U+FFFD for the broken
             * sequence and re-interpret this byte as fresh input.
             */
            if (parser->utf8_expected > 0) {
                if ((byte & 0xC0) == 0x80) {
                    parser->utf8_buffer[parser->utf8_received++] = byte;
                    if (parser->utf8_received == parser->utf8_expected) {
                        utf8_complete(parser);
                    }
                    break;
                }
                do_print(parser, 0xFFFD);
                utf8_reset(parser);
            }

            if (byte < 0x20) {
                /* C0 control character */
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte <= 0x7E) {
                /* Printable ASCII (0x20-0x7E) */
                do_print(parser, (uint32_t)byte);
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            else if ((byte & 0xE0) == 0xC0) {
                /* UTF-8 2-byte lead (110xxxxx) */
                parser->utf8_buffer[0] = byte;
                parser->utf8_expected = 2;
                parser->utf8_received = 1;
            }
            else if ((byte & 0xF0) == 0xE0) {
                /* UTF-8 3-byte lead (1110xxxx) */
                parser->utf8_buffer[0] = byte;
                parser->utf8_expected = 3;
                parser->utf8_received = 1;
            }
            else if ((byte & 0xF8) == 0xF0) {
                /* UTF-8 4-byte lead (11110xxx) */
                parser->utf8_buffer[0] = byte;
                parser->utf8_expected = 4;
                parser->utf8_received = 1;
            }
            else {
                /* Invalid byte or unexpected continuation */
                utf8_reset(parser);
                do_print(parser, 0xFFFD);
            }
            break;

        /*=== ESCAPE: Received ESC, waiting for next byte ===*/
        case WP_VT_ESCAPE:
            if (byte < 0x20) {
                /* C0 controls executed during ESC */
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x2F) {
                /* Intermediate byte -> ESCAPE_INTERMEDIATE */
                collect_intermediate(parser, byte);
                parser->state = WP_VT_ESCAPE_INTERMEDIATE;
            }
            else if (byte == 0x5B) {
                /* '[' -> CSI_ENTRY */
                clear_params(parser);
                parser->state = WP_VT_CSI_ENTRY;
            }
            else if (byte == 0x5D) {
                /* ']' -> OSC_STRING */
                clear_string(parser);
                parser->state = WP_VT_OSC_STRING;
            }
            else if (byte == 0x50) {
                /* 'P' -> DCS_ENTRY */
                clear_params(parser);
                clear_string(parser);
                parser->dcs_final = '\0';
                parser->state = WP_VT_DCS_ENTRY;
            }
            else if (byte == 0x58 || byte == 0x5E || byte == 0x5F) {
                /* 'X' (SOS), '^' (PM), '_' (APC) -> consume and ignore */
                clear_string(parser);
                parser->state = WP_VT_SOS_PM_APC_STRING;
            }
            else if (byte == 0x5C) {
                /*
                 * '\' = ST (String Terminator) as ESC \
                 *
                 * If we entered ESCAPE from a string-collecting state,
                 * this terminates that string. Otherwise it's a no-op.
                 */
                if (parser->pre_escape_state == WP_VT_OSC_STRING) {
                    do_osc_dispatch(parser);
                }
                else if (parser->pre_escape_state == WP_VT_DCS_PASSTHROUGH) {
                    do_dcs_dispatch(parser);
                }
                /* DCS_IGNORE, SOS_PM_APC_STRING: just discard */
                parser->state = WP_VT_GROUND;
            }
            else if (byte >= 0x30 && byte <= 0x7E) {
                /* Final byte -> dispatch ESC sequence, return to GROUND */
                do_esc_dispatch(parser, (char)byte);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== ESCAPE_INTERMEDIATE: ESC + intermediate byte(s) ===*/
        case WP_VT_ESCAPE_INTERMEDIATE:
            if (byte < 0x20) {
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x2F) {
                /* More intermediate bytes */
                collect_intermediate(parser, byte);
            }
            else if (byte >= 0x30 && byte <= 0x7E) {
                /* Final byte -> dispatch */
                do_esc_dispatch(parser, (char)byte);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== CSI_ENTRY: Received ESC [, waiting for params or final ===*/
        case WP_VT_CSI_ENTRY:
            if (byte < 0x20) {
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x2F) {
                /* Intermediate byte */
                collect_intermediate(parser, byte);
                parser->state = WP_VT_CSI_INTERMEDIATE;
            }
            else if (byte >= 0x30 && byte <= 0x39) {
                /* Digit -> start collecting parameter */
                collect_digit(parser, byte);
                parser->state = WP_VT_CSI_PARAM;
            }
            else if (byte == 0x3A) {
                /* ':' in entry state is an error */
                parser->state = WP_VT_CSI_IGNORE;
            }
            else if (byte == 0x3B) {
                /* ';' -> push empty (default) parameter */
                push_param(parser);
                parser->state = WP_VT_CSI_PARAM;
            }
            else if (byte >= 0x3C && byte <= 0x3F) {
                /*
                 * Private marker: '<', '=', '>', '?'
                 * Stored as an intermediate so dispatch can check for it.
                 * e.g., CSI ? 25 h -> intermediate '?', param 25, final 'h'
                 */
                collect_intermediate(parser, byte);
                parser->state = WP_VT_CSI_PARAM;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                /* Final byte with no parameters */
                do_csi_dispatch(parser, (char)byte);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== CSI_PARAM: Accumulating parameter digits ===*/
        case WP_VT_CSI_PARAM:
            if (byte < 0x20) {
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x2F) {
                /* Intermediate byte -> CSI_INTERMEDIATE */
                collect_intermediate(parser, byte);
                parser->state = WP_VT_CSI_INTERMEDIATE;
            }
            else if (byte >= 0x30 && byte <= 0x39) {
                /* Digit */
                collect_digit(parser, byte);
            }
            else if (byte == 0x3A) {
                /* ':' colon sub-parameter separator (e.g., SGR 38:2:R:G:B) */
                parser->has_subparams = true;
                push_param(parser);
            }
            else if (byte == 0x3B) {
                /* ';' semicolon parameter separator */
                push_param(parser);
            }
            else if (byte >= 0x3C && byte <= 0x3F) {
                /* Private marker after params started - malformed */
                parser->state = WP_VT_CSI_IGNORE;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                /* Final byte -> dispatch */
                do_csi_dispatch(parser, (char)byte);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== CSI_INTERMEDIATE: Intermediate byte(s) after params ===*/
        case WP_VT_CSI_INTERMEDIATE:
            if (byte < 0x20) {
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x2F) {
                /* More intermediate bytes */
                collect_intermediate(parser, byte);
            }
            else if (byte >= 0x30 && byte <= 0x3F) {
                /* Parameter byte after intermediate - malformed */
                parser->state = WP_VT_CSI_IGNORE;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                /* Final byte -> dispatch */
                do_csi_dispatch(parser, (char)byte);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== CSI_IGNORE: Malformed CSI, consume until final byte ===*/
        case WP_VT_CSI_IGNORE:
            if (byte < 0x20) {
                if (is_c0_executable(byte)) {
                    do_execute(parser, byte);
                }
            }
            else if (byte >= 0x20 && byte <= 0x3F) {
                /* Consume and ignore */
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                /* Final byte ends the malformed sequence */
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            break;

        /*=== OSC_STRING: Collecting Operating System Command ===*/
        case WP_VT_OSC_STRING:
            if (byte == 0x07) {
                /* BEL terminates OSC (xterm extension, widely used) */
                do_osc_dispatch(parser);
                parser->state = WP_VT_GROUND;
            }
            else if (byte == 0x9C) {
                /* 8-bit ST terminates OSC */
                do_osc_dispatch(parser);
                parser->state = WP_VT_GROUND;
            }
            else if (byte >= 0x20 || byte == 0x09) {
                /* Printable characters and HT collected into string buffer.
                 * Bytes > 0x7F are also collected (UTF-8 payload in OSC). */
                collect_string(parser, byte);
            }
            /* Other C0 controls silently ignored.
             * ESC handled by "anywhere" -> ESCAPE state with pre_escape_state
             * set, so ESC \ will dispatch via ST detection. */
            break;

        /*=== DCS_ENTRY: Device Control String parameter start ===*/
        case WP_VT_DCS_ENTRY:
            if (byte >= 0x20 && byte <= 0x2F) {
                collect_intermediate(parser, byte);
                parser->state = WP_VT_DCS_INTERMEDIATE;
            }
            else if (byte >= 0x30 && byte <= 0x39) {
                collect_digit(parser, byte);
                parser->state = WP_VT_DCS_PARAM;
            }
            else if (byte == 0x3A) {
                /* ':' in entry - error */
                parser->state = WP_VT_DCS_IGNORE;
            }
            else if (byte == 0x3B) {
                push_param(parser);
                parser->state = WP_VT_DCS_PARAM;
            }
            else if (byte >= 0x3C && byte <= 0x3F) {
                collect_intermediate(parser, byte);
                parser->state = WP_VT_DCS_PARAM;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                /* Final byte -> enter passthrough */
                parser->dcs_final = (char)byte;
                parser->state = WP_VT_DCS_PASSTHROUGH;
            }
            /* C0 controls and DEL ignored in DCS states */
            break;

        /*=== DCS_PARAM: DCS parameter digits ===*/
        case WP_VT_DCS_PARAM:
            if (byte >= 0x20 && byte <= 0x2F) {
                collect_intermediate(parser, byte);
                parser->state = WP_VT_DCS_INTERMEDIATE;
            }
            else if (byte >= 0x30 && byte <= 0x39) {
                collect_digit(parser, byte);
            }
            else if (byte == 0x3A) {
                parser->has_subparams = true;
                push_param(parser);
            }
            else if (byte == 0x3B) {
                push_param(parser);
            }
            else if (byte >= 0x3C && byte <= 0x3F) {
                /* Private marker after params - error */
                parser->state = WP_VT_DCS_IGNORE;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                parser->dcs_final = (char)byte;
                parser->state = WP_VT_DCS_PASSTHROUGH;
            }
            break;

        /*=== DCS_INTERMEDIATE: DCS intermediate byte(s) ===*/
        case WP_VT_DCS_INTERMEDIATE:
            if (byte >= 0x20 && byte <= 0x2F) {
                collect_intermediate(parser, byte);
            }
            else if (byte >= 0x30 && byte <= 0x3F) {
                /* Parameter byte after intermediate - error */
                parser->state = WP_VT_DCS_IGNORE;
            }
            else if (byte >= 0x40 && byte <= 0x7E) {
                parser->dcs_final = (char)byte;
                parser->state = WP_VT_DCS_PASSTHROUGH;
            }
            break;

        /*=== DCS_PASSTHROUGH: Collecting DCS payload data ===*/
        case WP_VT_DCS_PASSTHROUGH:
            if (byte == 0x9C) {
                /* 8-bit ST terminates DCS */
                do_dcs_dispatch(parser);
                parser->state = WP_VT_GROUND;
            }
            else if (byte >= 0x20 && byte <= 0x7E) {
                /* Data bytes collected into string buffer */
                collect_string(parser, byte);
            }
            else if (byte == 0x7F) {
                /* DEL - ignored */
            }
            /* ESC -> "anywhere" transition, ST detection via pre_escape_state.
             * CAN/SUB -> "anywhere" cancel. */
            break;

        /*=== DCS_IGNORE: Malformed DCS, waiting for ST ===*/
        case WP_VT_DCS_IGNORE:
            if (byte == 0x9C) {
                parser->state = WP_VT_GROUND;
            }
            /* Everything else ignored. ESC \ handled via "anywhere" + ESCAPE. */
            break;

        /*=== SOS_PM_APC_STRING: Consuming ignored string ===*/
        case WP_VT_SOS_PM_APC_STRING:
            if (byte == 0x9C) {
                /* 8-bit ST */
                parser->state = WP_VT_GROUND;
            }
            /* Everything consumed and discarded. ESC \ via "anywhere". */
            break;

        } /* switch (parser->state) */
    } /* for each byte */
}
