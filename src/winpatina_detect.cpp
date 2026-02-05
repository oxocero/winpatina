/**
 * @file winpatina_detect.cpp
 * @brief Capability detection for WinPatina
 *
 * Detects Windows version, terminal type, and available console features.
 */

#include "internal.h"
#include <cstdlib>   /* getenv */
#include <cstring>   /* strcmp */

/*============================================================================
 * Windows Version Detection
 *============================================================================*/

/**
 * @brief Classify Windows version from OSVERSIONINFOW
 *
 * Maps the major/minor/build numbers to our WinPatinaWindowsVersion enum.
 */
static WinPatinaWindowsVersion classify_windows_version(const OSVERSIONINFOW* osvi)
{
    DWORD major = osvi->dwMajorVersion;
    DWORD minor = osvi->dwMinorVersion;
    DWORD build = osvi->dwBuildNumber;

    if (major == 5) {
        if (minor == 0) {
            return WP_WIN_2000;     /* NT 5.0 */
        }
        return WP_WIN_XP;           /* NT 5.1 (XP) or 5.2 (Server 2003) */
    }

    if (major == 6) {
        if (minor == 0) {
            return WP_WIN_VISTA;    /* NT 6.0 */
        }
        if (minor == 1) {
            return WP_WIN_7;        /* NT 6.1 */
        }
        return WP_WIN_8;            /* NT 6.2 (8) or 6.3 (8.1) */
    }

    if (major == 10 || major > 10) {
        /* Windows 10 and 11 both report major version 10 */
        if (build >= 22000) {
            return WP_WIN_11;       /* Build 22000+ is Windows 11 */
        }
        if (build >= 10586) {
            return WP_WIN_10_VT;    /* Build 10586+ (version 1511+) has VT support */
        }
        return WP_WIN_10_LEGACY;    /* Early Windows 10, no VT support */
    }

    /* Older than Windows 2000? Treat as Windows 2000 */
    return WP_WIN_2000;
}

/*============================================================================
 * Terminal Type Detection
 *============================================================================*/

/**
 * @brief Detect terminal type from environment and console properties
 *
 * Checks environment variables that various terminals set as signatures.
 */
static WinPatinaTerminalType detect_terminal_type(void)
{
    /* Check for Windows Terminal (sets WT_SESSION) */
    if (getenv("WT_SESSION") != NULL) {
        return WP_TERM_WINDOWS_TERMINAL;
    }

    /* Check for ConEmu (sets ConEmuANSI=ON when ANSI is enabled) */
    const char* conemu = getenv("ConEmuANSI");
    if (conemu != NULL && strcmp(conemu, "ON") == 0) {
        return WP_TERM_CONEMU;
    }

    /* Check for mintty (Cygwin/MSYS2 terminal) */
    const char* term_program = getenv("TERM_PROGRAM");
    if (term_program != NULL && strcmp(term_program, "mintty") == 0) {
        return WP_TERM_MINTTY;
    }

    /*
     * PDCurses technique: Windows Terminal doesn't have a window icon.
     * If GetConsoleWindow() returns a handle but WM_GETICON returns 0,
     * we're likely in Windows Terminal.
     */
    HWND console_window = GetConsoleWindow();
    if (console_window != NULL) {
        LRESULT icon = SendMessageW(console_window, WM_GETICON, ICON_SMALL, 0);
        if (icon == 0) {
            /* No icon - likely Windows Terminal or similar modern terminal */
            return WP_TERM_WINDOWS_TERMINAL;
        }
    }

    /* Default to legacy console */
    return WP_TERM_LEGACY_CONSOLE;
}

