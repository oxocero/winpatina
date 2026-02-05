/**
 * @file main.cpp
 * @brief WinPatina command-line interface
 *
 * Simple CLI that displays detected system capabilities.
 * This serves as both a test tool and a usage example.
 */

#include "winpatina.h"
#include <cstdio>

/*============================================================================
 * Helper Functions - Enum to String Conversion
 *============================================================================*/

static const char* mode_to_string(WinPatinaMode mode)
{
    switch (mode) {
        case WP_MODE_VT_PASSTHROUGH:    return "VT Passthrough";
        case WP_MODE_WIN32_TRANSLATION: return "Win32 Translation";
        case WP_MODE_HYBRID:            return "Hybrid";
        default:                        return "Unknown";
    }
}

static const char* windows_version_to_string(WinPatinaWindowsVersion version)
{
    switch (version) {
        case WP_WIN_2000:       return "Windows 2000";
        case WP_WIN_XP:         return "Windows XP";
        case WP_WIN_VISTA:      return "Windows Vista";
        case WP_WIN_7:          return "Windows 7";
        case WP_WIN_8:          return "Windows 8/8.1";
        case WP_WIN_10_LEGACY:  return "Windows 10 (pre-1511)";
        case WP_WIN_10_VT:      return "Windows 10 (1511+)";
        case WP_WIN_11:         return "Windows 11";
        default:                return "Unknown";
    }
}

static const char* terminal_type_to_string(WinPatinaTerminalType type)
{
    switch (type) {
        case WP_TERM_LEGACY_CONSOLE:    return "Legacy Console (conhost)";
        case WP_TERM_WINDOWS_TERMINAL:  return "Windows Terminal";
        case WP_TERM_CONEMU:            return "ConEmu";
        case WP_TERM_MINTTY:            return "mintty";
        case WP_TERM_OTHER_VT:          return "Other VT-capable";
        case WP_TERM_UNKNOWN:           return "Unknown";
        default:                        return "Unknown";
    }
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char* argv[])
{
    (void)argc;  /* Unused for now */
    (void)argv;  /* Unused for now */

    printf("WinPatina v%s\n", wp_version_string());
    printf("================\n\n");

    /* Initialise with default configuration */
    WinPatina* wp = wp_init(NULL);
    if (wp == NULL) {
        printf("Error: %s\n", wp_get_error());
        return 1;
    }

    /* Display detected information */
    printf("System Information:\n");
    printf("  Windows Version:  %s\n", windows_version_to_string(wp_get_windows_version(wp)));
    printf("  Terminal Type:    %s\n", terminal_type_to_string(wp_get_terminal_type(wp)));
    printf("  Operating Mode:   %s\n", mode_to_string(wp_get_mode(wp)));
    printf("\n");

    /* Display screen size */
    int width, height;
    wp_get_screen_size(wp, &width, &height);
    printf("Screen Size:\n");
    printf("  %d columns x %d rows\n", width, height);
    printf("\n");

    /* Display capabilities */
    uint32_t caps = wp_get_capabilities(wp);
    printf("Capabilities:\n");
    printf("  [%c] VT Processing\n",  (caps & WP_CAP_VT_PROCESSING) ? 'x' : ' ');
    printf("  [%c] VT Input\n",       (caps & WP_CAP_VT_INPUT)      ? 'x' : ' ');
    printf("  [%c] UTF-8 Codepage\n", (caps & WP_CAP_UTF8_CODEPAGE) ? 'x' : ' ');
    printf("  [%c] Underline\n",      (caps & WP_CAP_UNDERSCORE)    ? 'x' : ' ');
    printf("  [%c] Grid Lines\n",     (caps & WP_CAP_GRID_LINES)    ? 'x' : ' ');
    printf("  [%c] 256 Colours\n",    (caps & WP_CAP_256_COLOURS)   ? 'x' : ' ');
    printf("  [%c] True Colour\n",    (caps & WP_CAP_TRUECOLOUR)    ? 'x' : ' ');
    printf("  [%c] Mouse Input\n",    (caps & WP_CAP_MOUSE)         ? 'x' : ' ');
    printf("  [%c] Alt Buffer\n",     (caps & WP_CAP_ALT_BUFFER)    ? 'x' : ' ');
    printf("  [%c] Sync Output\n",    (caps & WP_CAP_SYNC_OUTPUT)   ? 'x' : ' ');
    printf("\n");

    /* Clean up */
    wp_destroy(wp);

    return 0;
}
