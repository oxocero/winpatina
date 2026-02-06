/**
 * @file winpatina_vt_parser.h
 * @brief VT escape sequence parser - internal header
 *
 * State machine for parsing VT/ANSI escape sequences, based on the
 * Paul Williams VT parser model (vt100.net/emu/dec_ansi_parser).
 *
 * The parser processes a stream of bytes and invokes callbacks when
 * complete sequences are recognised. It does not interpret sequences
 * itself -- that is left to the callback implementations (screen buffer,
 * renderer, etc.).
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_VT_PARSER_H
#define WINPATINA_VT_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of CSI/DCS parameters */
#define WP_MAX_PARAMS 16

/** Maximum number of intermediate characters in a sequence */
#define WP_MAX_INTERMEDIATES 4

/** Maximum length of an OSC/DCS string payload */
#define WP_MAX_STRING_LEN 4096

/*============================================================================
 * State Machine States
 *============================================================================*/

/**
 * @brief Parser state machine states
 *
 * Simplified from the full Paul Williams model to cover the sequences
 * WinPatina needs to handle. States not relevant to our use case
 * (e.g., full VT52 compatibility) are omitted.
 */
typedef enum {
    WP_VT_GROUND,               /**< Normal text processing */
    WP_VT_ESCAPE,               /**< Received ESC (0x1B) */
    WP_VT_ESCAPE_INTERMEDIATE,  /**< ESC followed by intermediate byte (0x20-0x2F) */
    WP_VT_CSI_ENTRY,            /**< Received ESC [ (CSI) */
    WP_VT_CSI_PARAM,            /**< Accumulating CSI parameter digits */
    WP_VT_CSI_INTERMEDIATE,     /**< CSI intermediate bytes (0x20-0x2F) */
    WP_VT_CSI_IGNORE,           /**< Malformed CSI -- consume bytes until final */
    WP_VT_OSC_STRING,           /**< Operating System Command string */
    WP_VT_DCS_ENTRY,            /**< Device Control String entry */
    WP_VT_DCS_PARAM,            /**< DCS parameter digits */
    WP_VT_DCS_INTERMEDIATE,     /**< DCS intermediate bytes */
    WP_VT_DCS_PASSTHROUGH,      /**< DCS data passthrough */
    WP_VT_DCS_IGNORE,           /**< Malformed DCS -- consume until ST */
    WP_VT_SOS_PM_APC_STRING     /**< SOS/PM/APC string (consumed and ignored) */
} WPVTState;

/*============================================================================
 * Parser Structure
 *============================================================================*/

/* Forward declaration for callback signatures */
typedef struct WPVTParser WPVTParser;

/**
 * @brief Callback: printable character(s) received
 *
 * Called when one or more printable characters have been decoded
 * (including UTF-8 multibyte sequences decoded to UTF-16).
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param codepoint   Unicode codepoint of the character
 */
typedef void (*wp_vt_on_print_fn)(void* user_data, uint32_t codepoint);

/**
 * @brief Callback: C0 control character received
 *
 * Called for control characters in the 0x00-0x1F range (except ESC)
 * that are executed immediately: BEL (0x07), BS (0x08), HT (0x09),
 * LF (0x0A), VT (0x0B), FF (0x0C), CR (0x0D).
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param byte        The control character byte
 */
typedef void (*wp_vt_on_execute_fn)(void* user_data, uint8_t byte);

/**
 * @brief Callback: CSI sequence dispatched
 *
 * Called when a complete CSI sequence has been parsed. The parser
 * structure contains the accumulated parameters, intermediates,
 * and the final byte that identifies the sequence.
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param parser      Parser state (read params, intermediates from here)
 * @param final_byte  The final character of the CSI sequence (0x40-0x7E)
 */
typedef void (*wp_vt_on_csi_fn)(void* user_data, const WPVTParser* parser,
                                char final_byte);

/**
 * @brief Callback: ESC sequence dispatched
 *
 * Called when a complete ESC sequence (that isn't CSI/OSC/DCS) has been
 * parsed. For example, ESC 7 (DECSC) or ESC M (RI).
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param parser      Parser state (read intermediates from here)
 * @param final_byte  The final character of the ESC sequence
 */
typedef void (*wp_vt_on_esc_fn)(void* user_data, const WPVTParser* parser,
                                char final_byte);

/**
 * @brief Callback: OSC sequence dispatched
 *
 * Called when a complete Operating System Command has been received.
 * The string payload is available in the parser's string buffer.
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param parser      Parser state (read string_buffer, string_len from here)
 */
typedef void (*wp_vt_on_osc_fn)(void* user_data, const WPVTParser* parser);

/**
 * @brief Callback: DCS sequence dispatched
 *
 * Called when a Device Control String has been received.
 *
 * @param user_data   Opaque pointer from parser initialisation
 * @param parser      Parser state
 * @param final_byte  The final character that introduced the DCS data
 */
typedef void (*wp_vt_on_dcs_fn)(void* user_data, const WPVTParser* parser,
                                char final_byte);

/**
 * @brief VT parser state machine
 *
 * Tracks the current state, accumulated parameters, and registered
 * callbacks. Instantiate with wp_vt_parser_init().
 */
struct WPVTParser {
    /** Current state machine state */
    WPVTState state;

    /**
     * State before the most recent ESC "anywhere" transition.
     * Used to detect String Terminator (ESC \) when leaving a string
     * state (OSC, DCS). Without this, we cannot distinguish ESC \ as
     * ST from a regular ESC + '\' dispatch.
     */
    WPVTState pre_escape_state;

    /** Final byte that introduced a DCS passthrough (0x40-0x7E) */
    char dcs_final;

    /*--- CSI/DCS parameter accumulation ---*/

    /** Parsed parameter values */
    int params[WP_MAX_PARAMS];

    /** Number of parameters parsed so far */
    int param_count;

    /** Value being accumulated for the current parameter */
    int current_param;

    /** Whether the current parameter has received any digits */
    bool param_has_value;

    /**
     * Whether a colon sub-parameter separator was seen.
     * Colon-separated sub-parameters are used in SGR sequences
     * (e.g., "38:2:R:G:B" as an alternative to "38;2;R;G;B").
     */
    bool has_subparams;

    /*--- Intermediate characters ---*/

    /** Intermediate bytes (0x20-0x2F) collected during the sequence */
    char intermediates[WP_MAX_INTERMEDIATES];

    /** Number of intermediate characters collected */
    int intermediate_count;

    /*--- String accumulation (OSC, DCS) ---*/

    /** Buffer for OSC/DCS string payloads */
    char string_buffer[WP_MAX_STRING_LEN];

    /** Current length of the string in string_buffer */
    int string_len;

    /*--- UTF-8 accumulation ---*/

    /** Buffer for incomplete UTF-8 multibyte sequences */
    uint8_t utf8_buffer[4];

    /** Number of bytes expected in the current UTF-8 sequence */
    int utf8_expected;

    /** Number of bytes received so far for the current UTF-8 sequence */
    int utf8_received;

    /*--- Callbacks ---*/

    /** User data pointer passed to all callbacks */
    void* user_data;

    wp_vt_on_print_fn   on_print;       /**< Printable character */
    wp_vt_on_execute_fn  on_execute;     /**< C0 control character */
    wp_vt_on_csi_fn      on_csi;         /**< CSI sequence complete */
    wp_vt_on_esc_fn      on_esc;         /**< ESC sequence complete */
    wp_vt_on_osc_fn      on_osc;         /**< OSC sequence complete */
    wp_vt_on_dcs_fn      on_dcs;         /**< DCS sequence complete */
};

/*============================================================================
 * Parser Functions
 *============================================================================*/

/**
 * @brief Initialise a VT parser
 *
 * Sets the parser to the GROUND state with all accumulators cleared.
 * Callbacks are set to NULL and must be assigned after initialisation.
 *
 * @param parser     Parser to initialise
 * @param user_data  Opaque pointer passed to all callbacks
 */
void wp_vt_parser_init(WPVTParser* parser, void* user_data);

/**
 * @brief Feed data to the parser
 *
 * Processes a buffer of bytes through the state machine. Callbacks
 * will be invoked as sequences are recognised. The input is expected
 * to be UTF-8 encoded.
 *
 * It is safe to call this with any buffer size, including partial
 * sequences -- the parser maintains state between calls.
 *
 * @param parser  Parser instance
 * @param data    Input byte buffer
 * @param len     Number of bytes in the buffer
 */
void wp_vt_parser_feed(WPVTParser* parser, const uint8_t* data, size_t len);

/**
 * @brief Reset the parser to its initial state
 *
 * Clears all accumulators and returns to the GROUND state.
 * Callbacks and user_data are preserved.
 *
 * @param parser  Parser instance
 */
void wp_vt_parser_reset(WPVTParser* parser);

/*============================================================================
 * Parser Utility Functions
 *============================================================================*/

/**
 * @brief Get a CSI parameter value with a default
 *
 * Returns the parameter at the given index, or the default value if
 * the index is out of range or the parameter was omitted (empty).
 *
 * Many CSI sequences treat omitted parameters as 1 (e.g., CSI H
 * with no params means row 1, column 1). This function makes that
 * pattern easy: wp_vt_get_param(parser, 0, 1).
 *
 * @param parser         Parser instance
 * @param index          Parameter index (0-based)
 * @param default_value  Value to return if parameter is missing
 * @return The parameter value, or default_value
 */
int wp_vt_get_param(const WPVTParser* parser, int index, int default_value);

/**
 * @brief Check whether a specific intermediate character is present
 *
 * Some sequences use intermediate bytes to modify their meaning.
 * For example, CSI ? 25 h uses '?' as a "private" marker.
 *
 * @param parser  Parser instance
 * @param ch      The intermediate character to look for
 * @return true if ch is in the intermediates list
 */
bool wp_vt_has_intermediate(const WPVTParser* parser, char ch);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_VT_PARSER_H */
