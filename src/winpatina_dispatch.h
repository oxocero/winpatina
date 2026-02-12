/**
 * @file winpatina_dispatch.h
 * @brief VT sequence dispatch handler - internal header
 *
 * Connects the VT parser to the screen buffer and colour system.
 * When the parser recognises a sequence, it fires a callback; the
 * dispatch handler interprets that sequence and calls the appropriate
 * screen buffer or colour functions.
 *
 * This is the central "glue" layer:
 *   VT Parser  -->  Dispatch Handler  -->  Screen Buffer + Colour
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_DISPATCH_H
#define WINPATINA_DISPATCH_H

#include "winpatina_vt_parser.h"
#include "winpatina_screen.h"
#include "winpatina_colour.h"
#include "winpatina_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Dispatch State
 *============================================================================*/

/**
 * @brief Write-back callback for query responses
 *
 * Some sequences (e.g., DSR - Device Status Report) require sending
 * a response back to the child process. This callback is invoked
 * when such a response needs to be sent.
 *
 * @param wb_data  Opaque pointer (typically a pipe handle)
 * @param data     Response bytes to send
 * @param len      Number of bytes
 */
typedef void (*wp_dispatch_write_back_fn)(void* wb_data,
                                          const uint8_t* data, size_t len);

/**
 * @brief Dispatch handler state
 *
 * Holds references to the screen buffer, SGR colour state, and
 * configuration needed to translate VT sequences into operations.
 */
typedef struct {
    /** Screen buffer to operate on */
    WPScreenBuffer* screen;

    /** SGR attribute state (colours, bold, underline, reverse) */
    WPSGRState sgr;

    /** Default Win32 attributes (for SGR reset, erase operations) */
    WORD default_attrs;

    /** Whether LVB attributes (underline) are available (Vista+) */
    bool has_lvb;

    /**
     * Last printed codepoint, for REP (CSI b) support.
     * Set to 0 when no preceding graphic character exists.
     */
    uint32_t last_print;

    /**
     * Input handler state.
     * Dispatch sets mouse/keyboard modes here when processing
     * DECSET/DECRST sequences from the child's output.
     * May be NULL if input handling is not active.
     */
    WPInputState* input;

    /**
     * Console input handle.
     * Used to toggle ENABLE_MOUSE_INPUT when the child
     * enables/disables mouse tracking via DECSET/DECRST.
     * May be INVALID_HANDLE_VALUE if not available.
     */
    HANDLE hConsoleInput;

    /**
     * Write-back callback for query responses (DSR, DA, etc.)
     * May be NULL if no write-back channel is available.
     */
    wp_dispatch_write_back_fn on_write_back;

    /** Opaque data passed to on_write_back */
    void* write_back_data;
} WPDispatchState;

/*============================================================================
 * Lifecycle
 *============================================================================*/

/**
 * @brief Initialise the dispatch state
 *
 * Sets up the dispatch handler with references to the screen buffer,
 * input state, and initial colour state derived from the default
 * attributes.
 *
 * @param state          Dispatch state to initialise
 * @param screen         Screen buffer to operate on
 * @param default_attrs  Default Win32 console attributes
 * @param has_lvb        True if LVB attributes (underline) are available
 * @param input          Input handler state (may be NULL)
 * @param hConsoleInput  Console input handle (INVALID_HANDLE_VALUE if N/A)
 */
void wp_dispatch_init(WPDispatchState* state, WPScreenBuffer* screen,
                      WORD default_attrs, bool has_lvb,
                      WPInputState* input, HANDLE hConsoleInput);

/**
 * @brief Attach the dispatch handler to a VT parser
 *
 * Registers the dispatch callbacks on the parser and sets the parser's
 * user_data to point to the dispatch state. After this call, feeding
 * data to the parser will drive the screen buffer.
 *
 * @param state   Dispatch state (must already be initialised)
 * @param parser  VT parser to attach to
 */
void wp_dispatch_attach(WPDispatchState* state, WPVTParser* parser);

/**
 * @brief Set the write-back callback for query responses
 *
 * @param state    Dispatch state
 * @param fn       Callback function (NULL to disable)
 * @param wb_data  Opaque data passed to the callback
 */
void wp_dispatch_set_write_back(WPDispatchState* state,
                                wp_dispatch_write_back_fn fn,
                                void* wb_data);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_DISPATCH_H */
