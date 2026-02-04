/**
 * @file winpatina.h
 * @brief WinPatina - VT to Win32 Console translation layer
 *
 * WinPatina enables modern terminal applications that emit VT/ANSI escape
 * sequences to run on legacy Windows systems (Windows 2000 through Windows 10
 * pre-1511) that lack native Virtual Terminal Processing support.
 *
 * On modern terminals, WinPatina passes VT sequences through unchanged.
 * On legacy systems, it translates them to Win32 Console API calls.
 */

#ifndef WINPATINA_H
#define WINPATINA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version Information
 *============================================================================*/

#define WINPATINA_VERSION_MAJOR 1
#define WINPATINA_VERSION_MINOR 0
#define WINPATINA_VERSION_PATCH 0

/*============================================================================
 * Operating Modes
 *============================================================================*/

/**
 * @brief Operating mode for WinPatina
 *
 * Determined automatically at initialisation based on detected capabilities.
 */
typedef enum {
    /** Full VT support available - pass sequences through unchanged */
    WP_MODE_VT_PASSTHROUGH,

    /** No VT support - translate all sequences to Win32 Console API */
    WP_MODE_WIN32_TRANSLATION,

    /** Partial VT support - use VT where possible, Win32 for the rest */
    WP_MODE_HYBRID
} WinPatinaMode;

/*============================================================================
 * Windows Version Detection
 *============================================================================*/

/**
 * @brief Detected Windows version
 *
 * Used to determine which console features are available.
 */
typedef enum {
    WP_WIN_2000,        /**< NT 5.0 - No UTF-8 codepage */
    WP_WIN_XP,          /**< NT 5.1 - UTF-8 codepage available */
    WP_WIN_VISTA,       /**< NT 6.0 - LVB_UNDERSCORE available */
    WP_WIN_7,           /**< NT 6.1 */
    WP_WIN_8,           /**< NT 6.2/6.3 */
    WP_WIN_10_LEGACY,   /**< 10.0 build < 10586 - No VT support */
    WP_WIN_10_VT,       /**< 10.0 build >= 10586 - VT available */
    WP_WIN_11           /**< 10.0 build >= 22000 */
} WinPatinaWindowsVersion;

/*============================================================================
 * Terminal Type Detection
 *============================================================================*/

/**
 * @brief Detected terminal emulator type
 *
 * Different terminals have different capabilities even on the same OS.
 */
typedef enum {
    WP_TERM_LEGACY_CONSOLE,   /**< Classic conhost.exe */
    WP_TERM_WINDOWS_TERMINAL, /**< Modern Windows Terminal (WT_SESSION) */
    WP_TERM_CONEMU,           /**< ConEmu with ANSI support (ConEmuANSI=ON) */
    WP_TERM_MINTTY,           /**< Cygwin/MSYS2 mintty (TERM_PROGRAM=mintty) */
    WP_TERM_OTHER_VT,         /**< Other VT-capable terminal */
    WP_TERM_UNKNOWN           /**< Could not determine */
} WinPatinaTerminalType;

/*============================================================================
 * Capability Flags
 *============================================================================*/

/**
 * @brief Capability bit flags
 *
 * These can be combined with bitwise OR. Query with wp_get_capabilities().
 */
typedef enum {
    WP_CAP_VT_PROCESSING = 0x0001,  /**< ENABLE_VIRTUAL_TERMINAL_PROCESSING works */
    WP_CAP_VT_INPUT      = 0x0002,  /**< ENABLE_VIRTUAL_TERMINAL_INPUT works */
    WP_CAP_UTF8_CODEPAGE = 0x0004,  /**< UTF-8 codepage (65001) available */
    WP_CAP_UNDERSCORE    = 0x0008,  /**< COMMON_LVB_UNDERSCORE available (Vista+) */
    WP_CAP_GRID_LINES    = 0x0010,  /**< COMMON_LVB_GRID_* available */
    WP_CAP_256_COLOURS   = 0x0020,  /**< 256-colour palette in VT mode */
    WP_CAP_TRUECOLOUR    = 0x0040,  /**< 24-bit RGB colour in VT mode */
    WP_CAP_MOUSE         = 0x0080,  /**< Mouse input available */
    WP_CAP_ALT_BUFFER    = 0x0100,  /**< Alternate screen buffer works */
    WP_CAP_SYNC_OUTPUT   = 0x0200   /**< Synchronised output (reduces flicker) */
} WinPatinaCapability;

/*============================================================================
 * Configuration
 *============================================================================*/

/**
 * @brief Configuration options for WinPatina initialisation
 *
 * Zero-initialise for sensible defaults:
 * @code
 * WinPatinaConfig config = {0};
 * @endcode
 */
typedef struct {
    /** Force Win32 translation mode even if VT is available (for testing) */
    bool force_translation;

    /** Enable mouse input translation */
    bool enable_mouse;

    /** Enable UTF-8 codepage if available (default: true when zero-init) */
    bool enable_utf8;

    /**
     * Colour mode: 0 = auto-detect, 16 = force 16-colour, 256 = force 256-colour
     * In translation mode, 256 colours are quantised to 16.
     */
    int colour_mode;
} WinPatinaConfig;

/*============================================================================
 * Opaque Handle
 *============================================================================*/

/**
 * @brief Opaque handle to a WinPatina instance
 *
 * Created with wp_init(), destroyed with wp_destroy().
 * The internal structure is hidden from users.
 */
typedef struct WinPatina WinPatina;

/*============================================================================
 * Core API - Lifecycle
 *============================================================================*/

/**
 * @brief Initialise WinPatina
 *
 * Detects terminal capabilities and sets up the console for operation.
 *
 * @param config Configuration options (NULL for defaults)
 * @return Handle to WinPatina instance, or NULL on failure
 *
 * @code
 * WinPatinaConfig config = {0};
 * config.enable_mouse = true;
 * WinPatina* wp = wp_init(&config);
 * if (!wp) {
 *     fprintf(stderr, "Failed to initialise WinPatina\n");
 *     return 1;
 * }
 * @endcode
 */
WinPatina* wp_init(const WinPatinaConfig* config);

/**
 * @brief Clean up and destroy WinPatina instance
 *
 * Restores the console to its original state and frees all resources.
 * Safe to call with NULL.
 *
 * @param wp Handle to destroy (may be NULL)
 */
void wp_destroy(WinPatina* wp);

/*============================================================================
 * Core API - Capability Queries
 *============================================================================*/

/**
 * @brief Get the operating mode
 *
 * @param wp WinPatina handle
 * @return The detected operating mode
 */
WinPatinaMode wp_get_mode(WinPatina* wp);

/**
 * @brief Get the detected Windows version
 *
 * @param wp WinPatina handle
 * @return The detected Windows version
 */
WinPatinaWindowsVersion wp_get_windows_version(WinPatina* wp);

/**
 * @brief Get the detected terminal type
 *
 * @param wp WinPatina handle
 * @return The detected terminal type
 */
WinPatinaTerminalType wp_get_terminal_type(WinPatina* wp);

/**
 * @brief Get capability flags
 *
 * @param wp WinPatina handle
 * @return Bitmask of WinPatinaCapability flags
 *
 * @code
 * uint32_t caps = wp_get_capabilities(wp);
 * if (caps & WP_CAP_MOUSE) {
 *     printf("Mouse input is available\n");
 * }
 * if (caps & WP_CAP_TRUECOLOUR) {
 *     printf("24-bit colour is available\n");
 * }
 * @endcode
 */
uint32_t wp_get_capabilities(WinPatina* wp);

/**
 * @brief Get current screen dimensions
 *
 * @param wp WinPatina handle
 * @param[out] width Receives the screen width in columns
 * @param[out] height Receives the screen height in rows
 */
void wp_get_screen_size(WinPatina* wp, int* width, int* height);

#endif /* WINPATINA_H */
