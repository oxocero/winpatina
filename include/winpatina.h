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

/**
 * @brief Get version as a single integer
 *
 * Format: (major * 10000) + (minor * 100) + patch
 * Example: version 1.2.3 returns 10203
 *
 * @return Version number
 */
int wp_version(void);

/**
 * @brief Get version as a string
 *
 * @return Version string (e.g., "1.0.0")
 */
const char* wp_version_string(void);

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

/*============================================================================
 * Core API - Process Management
 *============================================================================*/

/**
 * @brief Spawn a child process
 *
 * Launches the specified command with its stdin/stdout redirected through
 * WinPatina. The child's VT output will be translated (if needed) and
 * rendered to the console.
 *
 * @param wp WinPatina handle
 * @param command Path to the executable
 * @param argv Argument array (NULL-terminated, argv[0] is typically the program name)
 * @return 0 on success, -1 on failure
 *
 * @code
 * char* argv[] = {"myapp", "--option", NULL};
 * if (wp_spawn(wp, "myapp.exe", argv) != 0) {
 *     fprintf(stderr, "Failed to spawn process\n");
 * }
 * @endcode
 */
int wp_spawn(WinPatina* wp, const char* command, char* const argv[]);

/**
 * @brief Spawn the default shell
 *
 * Convenience function to spawn cmd.exe or the shell specified by COMSPEC.
 *
 * @param wp WinPatina handle
 * @return 0 on success, -1 on failure
 */
int wp_spawn_shell(WinPatina* wp);

/**
 * @brief Check if the child process is still running
 *
 * @param wp WinPatina handle
 * @return true if child is running, false if exited or not spawned
 */
bool wp_is_running(WinPatina* wp);

/**
 * @brief Get the child process exit code
 *
 * Only valid after wp_is_running() returns false.
 *
 * @param wp WinPatina handle
 * @return Exit code, or -1 if child is still running or was never spawned
 */
int wp_get_exit_code(WinPatina* wp);

/*============================================================================
 * Core API - Main Loop
 *============================================================================*/

/**
 * @brief Process events (non-blocking or with timeout)
 *
 * Reads console input, sends to child, reads child output, renders to screen.
 * Call this repeatedly in your own event loop.
 *
 * @param wp WinPatina handle
 * @param timeout_ms Maximum time to wait for events:
 *                   - 0 = non-blocking (return immediately)
 *                   - >0 = wait up to this many milliseconds
 *                   - -1 = wait indefinitely
 * @return 1 if child exited, 0 if still running, -1 on error
 *
 * @code
 * while (wp_poll(wp, 100) == 0) {
 *     // Child still running, poll returned after timeout or event
 * }
 * int exit_code = wp_get_exit_code(wp);
 * @endcode
 */
int wp_poll(WinPatina* wp, int timeout_ms);

/**
 * @brief Run the main loop until child exits
 *
 * Convenience function that calls wp_poll() in a loop until the child
 * process terminates. This is a blocking call.
 *
 * @param wp WinPatina handle
 * @return Child's exit code, or -1 on error
 *
 * @code
 * wp_spawn(wp, "myapp.exe", argv);
 * int exit_code = wp_run(wp);
 * printf("Child exited with code %d\n", exit_code);
 * @endcode
 */
int wp_run(WinPatina* wp);

/*============================================================================
 * Core API - Direct I/O
 *============================================================================*/

/**
 * @brief Write data directly to the child's stdin
 *
 * Bypasses normal input handling. Useful for injecting commands or data
 * programmatically.
 *
 * @param wp WinPatina handle
 * @param data Data to write
 * @param len Length of data in bytes
 * @return Number of bytes written, or -1 on error
 */
int wp_write_child(WinPatina* wp, const char* data, int len);

/**
 * @brief Force a full screen refresh
 *
 * Redraws the entire screen. Useful after window corruption or resize.
 * In translation mode, this re-renders from the internal screen buffer.
 * In passthrough mode, this is a no-op (the terminal handles it).
 *
 * @param wp WinPatina handle
 */
void wp_refresh(WinPatina* wp);

/*============================================================================
 * Core API - Terminal Control
 *============================================================================*/

/**
 * @brief Set the console window title
 *
 * @param wp WinPatina handle
 * @param title New window title (UTF-8 encoded)
 */
void wp_set_title(WinPatina* wp, const char* title);

/**
 * @brief Enable or disable mouse input
 *
 * When enabled, mouse events are translated to VT sequences and sent to
 * the child process. Can be toggled at runtime.
 *
 * @param wp WinPatina handle
 * @param enabled true to enable mouse input, false to disable
 */
void wp_set_mouse_enabled(WinPatina* wp, bool enabled);

/*============================================================================
 * Core API - Error Handling
 *============================================================================*/

/**
 * @brief Get the last error message
 *
 * Returns a human-readable description of the last error. The message is
 * stored in thread-local storage, so it's safe to call from multiple threads.
 *
 * @return Error message string, or NULL if no error has occurred
 *
 * @code
 * WinPatina* wp = wp_init(&config);
 * if (!wp) {
 *     fprintf(stderr, "Failed to initialise: %s\n", wp_get_error());
 *     return 1;
 * }
 * @endcode
 */
const char* wp_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_H */
