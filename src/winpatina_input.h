/**
 * @file winpatina_input.h
 * @brief Console input handler - internal header
 *
 * Translates Win32 console input events (keyboard, mouse, resize)
 * into VT/ANSI escape sequences suitable for writing to a child
 * process's stdin pipe.
 *
 * This is the reverse direction of the output pipeline:
 *   Console Input (Win32) --> Input Handler --> VT bytes --> Child
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_INPUT_H
#define WINPATINA_INPUT_H

#include <stdint.h>
#include <stdbool.h>

/* Windows headers */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/**
 * Maximum bytes a single input event can produce.
 *
 * Most sequences are short (e.g., ESC [ A = 3 bytes), but modified keys
 * with full CSI u encoding can be longer. 32 bytes is ample.
 */
#define WP_INPUT_BUF_MAX 32

/*============================================================================
 * Keyboard Mode
 *============================================================================*/

/**
 * @brief Cursor key mode (DECCKM)
 *
 * Controls whether arrow/home/end keys emit CSI (normal) or SS3
 * (application) sequences.
 */
typedef enum {
    WP_CURSOR_KEY_NORMAL,      /**< Arrow keys emit ESC [ A/B/C/D */
    WP_CURSOR_KEY_APPLICATION  /**< Arrow keys emit ESC O A/B/C/D */
} WPCursorKeyMode;

/**
 * @brief Keypad mode (DECKPAM / DECKPNM)
 *
 * Controls whether the numeric keypad emits digits (numeric) or
 * application-mode sequences.
 */
typedef enum {
    WP_KEYPAD_NUMERIC,         /**< Keypad emits digits */
    WP_KEYPAD_APPLICATION      /**< Keypad emits ESC O sequences */
} WPKeypadMode;

/**
 * @brief Mouse tracking mode
 *
 * Determines which mouse events are reported and in what format.
 */
typedef enum {
    WP_MOUSE_OFF,              /**< No mouse reporting */
    WP_MOUSE_X10,              /**< X10 compatibility: button press only */
    WP_MOUSE_NORMAL,           /**< Normal tracking: press and release */
    WP_MOUSE_BUTTON,           /**< Button-event: press, release, motion while held */
    WP_MOUSE_ANY               /**< Any-event: all motion, even without buttons */
} WPMouseMode;

/**
 * @brief Mouse encoding format
 */
typedef enum {
    WP_MOUSE_ENC_DEFAULT,      /**< X10-compatible: ESC [ M Cb Cx Cy (6-byte) */
    WP_MOUSE_ENC_SGR           /**< SGR: ESC [ < Pb ; Px ; Py M/m */
} WPMouseEncoding;

/*============================================================================
 * Input Handler State
 *============================================================================*/

/**
 * @brief Input handler state
 *
 * Holds mode flags that affect how input events are translated.
 * These modes are typically set by the dispatch handler when it
 * processes DECSET/DECRST sequences from the child's output.
 */
typedef struct {
    /** Cursor key mode (normal or application) */
    WPCursorKeyMode cursor_key_mode;

    /** Keypad mode (numeric or application) */
    WPKeypadMode keypad_mode;

    /** Mouse tracking mode */
    WPMouseMode mouse_mode;

    /** Mouse encoding format */
    WPMouseEncoding mouse_encoding;

    /**
     * Bracketed paste mode.
     * When enabled, pasted text is wrapped in ESC [200~ ... ESC [201~
     */
    bool bracketed_paste;

    /** Previous mouse button state for tracking releases */
    DWORD prev_mouse_buttons;
} WPInputState;

/*============================================================================
 * Lifecycle
 *============================================================================*/

/**
 * @brief Initialise the input handler state
 *
 * Sets all modes to their default (normal cursor keys, numeric keypad,
 * mouse off, bracketed paste off).
 *
 * @param state Input state to initialise
 */
void wp_input_init(WPInputState* state);

/*============================================================================
 * Event Translation
 *============================================================================*/

/**
 * @brief Translate a keyboard event to VT bytes
 *
 * Converts a Win32 KEY_EVENT_RECORD into one or more bytes of VT
 * escape sequences or UTF-8 text.
 *
 * @param state   Input handler state
 * @param event   Win32 key event record
 * @param buf     Output buffer (must be at least WP_INPUT_BUF_MAX bytes)
 * @return Number of bytes written to buf (0 if event produces no output)
 */
int wp_input_translate_key(const WPInputState* state,
                           const KEY_EVENT_RECORD* event,
                           uint8_t* buf);

/**
 * @brief Translate a mouse event to VT bytes
 *
 * Converts a Win32 MOUSE_EVENT_RECORD into VT mouse reporting
 * sequences, if mouse tracking is enabled.
 *
 * @param state   Input handler state (mouse_mode must be != WP_MOUSE_OFF)
 * @param event   Win32 mouse event record
 * @param buf     Output buffer (must be at least WP_INPUT_BUF_MAX bytes)
 * @return Number of bytes written to buf (0 if event produces no output)
 */
int wp_input_translate_mouse(WPInputState* state,
                             const MOUSE_EVENT_RECORD* event,
                             uint8_t* buf);

/**
 * @brief Encode a bracketed paste start marker
 *
 * Writes ESC [200~ to the buffer if bracketed paste is enabled.
 *
 * @param state  Input handler state
 * @param buf    Output buffer (must be at least WP_INPUT_BUF_MAX bytes)
 * @return Number of bytes written (0 if bracketed paste is disabled)
 */
int wp_input_paste_start(const WPInputState* state, uint8_t* buf);

/**
 * @brief Encode a bracketed paste end marker
 *
 * Writes ESC [201~ to the buffer if bracketed paste is enabled.
 *
 * @param state  Input handler state
 * @param buf    Output buffer (must be at least WP_INPUT_BUF_MAX bytes)
 * @return Number of bytes written (0 if bracketed paste is disabled)
 */
int wp_input_paste_end(const WPInputState* state, uint8_t* buf);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_INPUT_H */
