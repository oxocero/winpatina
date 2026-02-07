/**
 * @file main.cpp
 * @brief WinPatina command-line interface
 *
 * Usage:
 *   winpatina              Launch the default shell through the pipeline
 *   winpatina --info       Display detected system capabilities and exit
 *   winpatina --help       Show usage information
 *   winpatina <command>    Run a specific command through the pipeline
 *
 * Examples:
 *   winpatina              Spawns cmd.exe (or %COMSPEC%)
 *   winpatina --info       Shows Windows version, terminal type, capabilities
 *   winpatina python.exe   Runs Python through the translation layer
 */

#include "winpatina.h"
#include <cstdio>
#include <cstring>

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
 * --info: Display Capabilities
 *============================================================================*/

static int show_info(void)
{
    printf("WinPatina v%s\n", wp_version_string());
    printf("================\n\n");

    WinPatina* wp = wp_init(NULL);
    if (wp == NULL) {
        fprintf(stderr, "Error: %s\n", wp_get_error());
        return 1;
    }

    printf("System Information:\n");
    printf("  Windows Version:  %s\n", windows_version_to_string(wp_get_windows_version(wp)));
    printf("  Terminal Type:    %s\n", terminal_type_to_string(wp_get_terminal_type(wp)));
    printf("  Operating Mode:   %s\n", mode_to_string(wp_get_mode(wp)));
    printf("\n");

    int width, height;
    wp_get_screen_size(wp, &width, &height);
    printf("Screen Size:\n");
    printf("  %d columns x %d rows\n", width, height);
    printf("\n");

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

    wp_destroy(wp);
    return 0;
}

/*============================================================================
 * --help: Usage Information
 *============================================================================*/

static void show_help(void)
{
    printf("WinPatina v%s - VT to Win32 Console translation layer\n\n", wp_version_string());
    printf("Usage:\n");
    printf("  winpatina              Launch the default shell\n");
    printf("  winpatina --info       Display system capabilities\n");
    printf("  winpatina --help       Show this help message\n");
    printf("  winpatina <command>    Run a command through the pipeline\n");
    printf("\n");
    printf("Options:\n");
    printf("  --force-translate      Force Win32 translation mode\n");
    printf("\n");
    printf("Examples:\n");
    printf("  winpatina              Spawns %%COMSPEC%% (usually cmd.exe)\n");
    printf("  winpatina --info       Troubleshooting: shows detected capabilities\n");
    printf("  winpatina python.exe   Runs Python through the translation layer\n");
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char* argv[])
{
    bool force_translate = false;
    const char* command = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--info") == 0) {
            return show_info();
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help();
            return 0;
        }
        if (strcmp(argv[i], "--force-translate") == 0) {
            force_translate = true;
            continue;
        }
        /* First non-option argument is the command */
        if (command == NULL) {
            command = argv[i];
        }
    }

    /* Initialise WinPatina */
    WinPatinaConfig config;
    memset(&config, 0, sizeof(config));
    config.enable_utf8 = true;
    config.force_translation = force_translate;

    WinPatina* wp = wp_init(&config);
    if (wp == NULL) {
        fprintf(stderr, "winpatina: failed to initialise: %s\n", wp_get_error());
        return 1;
    }

    /* Spawn the child process */
    int spawn_result;
    if (command != NULL) {
        spawn_result = wp_spawn(wp, command, NULL);
    } else {
        spawn_result = wp_spawn_shell(wp);
    }

    if (spawn_result != 0) {
        fprintf(stderr, "winpatina: failed to spawn process: %s\n", wp_get_error());
        wp_destroy(wp);
        return 1;
    }

    /* Run until the child exits */
    int exit_code = wp_run(wp);

    /* Clean up */
    wp_destroy(wp);

    return (exit_code >= 0) ? exit_code : 1;
}
