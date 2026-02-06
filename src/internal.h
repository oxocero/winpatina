/**
 * @file internal.h
 * @brief WinPatina internal definitions
 *
 * This header contains internal structures and functions that are not
 * exposed in the public API. Only implementation files should include this.
 */

#ifndef WINPATINA_INTERNAL_H
#define WINPATINA_INTERNAL_H

#include "winpatina.h"
#include "winpatina_vt_parser.h"
#include "winpatina_screen.h"
#include "winpatina_colour.h"
#include "winpatina_dispatch.h"
#include "winpatina_render.h"
#include "winpatina_input.h"
#include "winpatina_process.h"

/* Windows headers - target Windows 2000 minimum */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#ifndef WINVER
#define WINVER 0x0500
#endif

#include <windows.h>

/*============================================================================
 * Console Mode Flags
 *
 * These may not be defined in older SDKs or MinGW headers.
 *============================================================================*/

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

#ifndef ENABLE_LVB_GRID_WORLDWIDE
#define ENABLE_LVB_GRID_WORLDWIDE 0x0010
#endif

/*============================================================================
 * LVB (Line Visual Buffer) Attribute Flags
 *
 * Used in CHAR_INFO.Attributes for text styling. Available from Vista+.
 *============================================================================*/

#ifndef COMMON_LVB_REVERSE_VIDEO
#define COMMON_LVB_REVERSE_VIDEO 0x4000
#endif

#ifndef COMMON_LVB_UNDERSCORE
#define COMMON_LVB_UNDERSCORE 0x8000
#endif

#ifndef COMMON_LVB_GRID_HORIZONTAL
#define COMMON_LVB_GRID_HORIZONTAL 0x0400
#endif

#ifndef COMMON_LVB_GRID_LVERTICAL
#define COMMON_LVB_GRID_LVERTICAL 0x0800
#endif

#ifndef COMMON_LVB_GRID_RVERTICAL
#define COMMON_LVB_GRID_RVERTICAL 0x1000
#endif

/*============================================================================
 * Internal Structures
 *============================================================================*/

/**
 * @brief Detected system capabilities
 *
 * Populated during wp_init() by the capability detector.
 */
typedef struct {
    WinPatinaMode mode;                 /**< Operating mode */
    WinPatinaWindowsVersion os_version; /**< Detected Windows version */
    WinPatinaTerminalType terminal;     /**< Detected terminal type */
    uint32_t flags;                     /**< Bitmask of WinPatinaCapability */
    int max_colours;                    /**< Maximum colours: 16, 256, or 16777216 */
    int screen_width;                   /**< Console width in columns */
    int screen_height;                  /**< Console height in rows */
} WinPatinaCapabilities;

/**
 * @brief Main WinPatina instance structure
 *
 * This is the actual definition of the opaque WinPatina handle.
 * Users only see a pointer; implementation files see the contents.
 */
struct WinPatina {
    /* Detected capabilities */
    WinPatinaCapabilities caps;

    /* User configuration (copy of what was passed to wp_init) */
    WinPatinaConfig config;

    /* Console handles */
    HANDLE hConsoleOutput;              /**< Standard output handle */
    HANDLE hConsoleInput;               /**< Standard input handle */

    /* Original console modes (to restore on cleanup) */
    DWORD original_output_mode;
    DWORD original_input_mode;

    /* Original console codepages (to restore on cleanup) */
    UINT original_output_cp;
    UINT original_input_cp;

    /*==================================================================
     * Pipeline Components (initialised when mode == WIN32_TRANSLATION)
     *==================================================================*/

    /** Screen buffer — cell grid + cursor + scroll regions */
    WPScreenBuffer* screen;

    /** VT parser — state machine that tokenises VT byte streams */
    WPVTParser parser;

    /** Dispatch — glue between parser callbacks and screen ops */
    WPDispatchState dispatch;

    /** Renderer — screen buffer to WriteConsoleOutputW */
    WPRenderer renderer;

    /** Input handler — Win32 input events to VT byte sequences */
    WPInputState input;

    /** Child process — pipes + process management */
    WPProcess process;

    /** Whether the pipeline has been set up */
    bool pipeline_ready;
};

/*============================================================================
 * Internal Functions - Error Handling
 *============================================================================*/

/**
 * @brief Set the last error message
 *
 * Stores the message in thread-local storage for retrieval via wp_get_error().
 *
 * @param message Error message (will be copied)
 */
void wp_set_error(const char* message);

/**
 * @brief Set error message with Win32 error code
 *
 * Formats a message that includes the Win32 error description.
 *
 * @param message Prefix message
 * @param error_code Win32 error code from GetLastError()
 */
void wp_set_error_win32(const char* message, DWORD error_code);

/*============================================================================
 * Internal Functions - Capability Detection
 *============================================================================*/

/**
 * @brief Detect system capabilities
 *
 * Probes the system to determine Windows version, terminal type,
 * and available features. Called during wp_init().
 *
 * @param[out] caps Structure to fill with detected capabilities
 * @param config User configuration (may affect detection)
 * @return true on success, false on failure
 */
bool wp_detect_capabilities(WinPatinaCapabilities* caps, const WinPatinaConfig* config);

#endif /* WINPATINA_INTERNAL_H */
