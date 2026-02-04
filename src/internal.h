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

#endif /* WINPATINA_INTERNAL_H */
