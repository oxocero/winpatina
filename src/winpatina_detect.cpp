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

/*============================================================================
 * Main Detection Function
 *============================================================================*/

bool wp_detect_capabilities(WinPatinaCapabilities* caps, const WinPatinaConfig* config)
{
    /* Zero-initialise the structure */
    memset(caps, 0, sizeof(*caps));

    /*
     * Step 1: Detect Windows version
     *
     * We use GetVersionExW which is deprecated but works on all Windows versions
     * from 2000 onwards. The "proper" replacement (VerifyVersionInfo) is more
     * complex and doesn't give us the build number easily.
     */
    OSVERSIONINFOW osvi;
    memset(&osvi, 0, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    #pragma warning(push)
    #pragma warning(disable: 4996)  /* Disable deprecation warning for GetVersionExW */
    if (!GetVersionExW(&osvi)) {
        wp_set_error_win32("Failed to get Windows version", GetLastError());
        return false;
    }
    #pragma warning(pop)

    caps->os_version = classify_windows_version(&osvi);

    /*
     * Step 2: Detect terminal type
     */
    caps->terminal = detect_terminal_type();

    /*
     * Step 3: Get console handles and current modes
     */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    if (hOut == INVALID_HANDLE_VALUE || hIn == INVALID_HANDLE_VALUE) {
        wp_set_error("Failed to get console handles - not running in a console?");
        return false;
    }

    DWORD output_mode = 0;
    DWORD input_mode = 0;

    if (!GetConsoleMode(hOut, &output_mode)) {
        /* Not a console handle - might be redirected to a file */
        wp_set_error("Standard output is not a console");
        return false;
    }

    GetConsoleMode(hIn, &input_mode);  /* Input might fail if redirected, that's OK */

    /*
     * Step 4: Test VT processing support
     *
     * Try to enable ENABLE_VIRTUAL_TERMINAL_PROCESSING. If it succeeds,
     * the console supports VT sequences.
     */
    if (SetConsoleMode(hOut, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        caps->flags |= WP_CAP_VT_PROCESSING;

        /* VT mode also implies these capabilities */
        caps->flags |= WP_CAP_256_COLOURS;
        caps->flags |= WP_CAP_TRUECOLOUR;
        caps->flags |= WP_CAP_ALT_BUFFER;
        caps->flags |= WP_CAP_SYNC_OUTPUT;

        /* Restore original mode for now */
        SetConsoleMode(hOut, output_mode);
    }

    /*
     * Step 5: Test VT input support
     */
    if (SetConsoleMode(hIn, input_mode | ENABLE_VIRTUAL_TERMINAL_INPUT)) {
        caps->flags |= WP_CAP_VT_INPUT;
        SetConsoleMode(hIn, input_mode);
    }

    /*
     * Step 6: Test LVB attribute support (Vista+)
     */
    if (SetConsoleMode(hOut, output_mode | ENABLE_LVB_GRID_WORLDWIDE)) {
        caps->flags |= WP_CAP_UNDERSCORE;
        caps->flags |= WP_CAP_GRID_LINES;
        SetConsoleMode(hOut, output_mode);
    }

    /*
     * Step 7: Check UTF-8 codepage availability
     */
    if (IsValidCodePage(CP_UTF8)) {
        caps->flags |= WP_CAP_UTF8_CODEPAGE;
    }

    /*
     * Step 8: Mouse support is available on all Windows versions
     * (via ReadConsoleInput), but we only report it if enabled in config
     */
    caps->flags |= WP_CAP_MOUSE;

    /*
     * Step 9: Determine operating mode
     */
    if (config != NULL && config->force_translation) {
        /* User requested forced translation mode */
        caps->mode = WP_MODE_WIN32_TRANSLATION;
        caps->max_colours = 16;
    } else if (caps->flags & WP_CAP_VT_PROCESSING) {
        /* VT support available - use passthrough */
        caps->mode = WP_MODE_VT_PASSTHROUGH;
        caps->max_colours = 16777216;  /* 24-bit true colour */
    } else {
        /* No VT support - must translate */
        caps->mode = WP_MODE_WIN32_TRANSLATION;
        caps->max_colours = 16;
    }

    /*
     * Step 10: Get screen size
     */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        caps->screen_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        caps->screen_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        /* Default to standard size if we can't determine */
        caps->screen_width = 80;
        caps->screen_height = 25;
    }

    return true;
}
