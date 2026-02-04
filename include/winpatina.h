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

#endif /* WINPATINA_H */
