/**
 * @file winpatina_main.cpp
 * @brief Core WinPatina implementation
 *
 * Implements the main public API: initialisation, destruction, and queries.
 */

#include "internal.h"
#include <cstdio>    /* snprintf */
#include <cstdlib>   /* malloc, free */
#include <cstring>   /* strncpy, strlen */

/*============================================================================
 * Error Handling
 *
 * Thread-local storage for error messages.
 *============================================================================*/

#define WP_ERROR_BUFFER_SIZE 512

/* Thread-local error buffer */
/* Use thread_local (C++11) which works on both MSVC and GCC/MinGW */
static thread_local char tls_error_buffer[WP_ERROR_BUFFER_SIZE];
static thread_local bool tls_error_set = false;

void wp_set_error(const char* message)
{
    if (message == NULL) {
        tls_error_buffer[0] = '\0';
        tls_error_set = false;
        return;
    }

    strncpy(tls_error_buffer, message, WP_ERROR_BUFFER_SIZE - 1);
    tls_error_buffer[WP_ERROR_BUFFER_SIZE - 1] = '\0';
    tls_error_set = true;
}

void wp_set_error_win32(const char* message, DWORD error_code)
{
    char win32_message[256] = {0};

    /* Get the Windows error message */
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        win32_message,
        sizeof(win32_message) - 1,
        NULL
    );

    /* Remove trailing newline if present */
    size_t len = strlen(win32_message);
    while (len > 0 && (win32_message[len - 1] == '\n' || win32_message[len - 1] == '\r')) {
        win32_message[--len] = '\0';
    }

    /* Format: "message: win32_message (error N)" */
    snprintf(tls_error_buffer, WP_ERROR_BUFFER_SIZE,
             "%s: %s (error %lu)",
             message ? message : "Error",
             win32_message,
             (unsigned long)error_code);

    tls_error_set = true;
}

const char* wp_get_error(void)
{
    if (!tls_error_set) {
        return NULL;
    }
    return tls_error_buffer;
}

/*============================================================================
 * Version Functions
 *============================================================================*/

int wp_version(void)
{
    return (WINPATINA_VERSION_MAJOR * 10000) +
           (WINPATINA_VERSION_MINOR * 100) +
           WINPATINA_VERSION_PATCH;
}

const char* wp_version_string(void)
{
    /* Static buffer - constructed once on first call */
    static char version_str[32] = {0};

    if (version_str[0] == '\0') {
        snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                 WINPATINA_VERSION_MAJOR,
                 WINPATINA_VERSION_MINOR,
                 WINPATINA_VERSION_PATCH);
    }

    return version_str;
}

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

WinPatina* wp_init(const WinPatinaConfig* config)
{
    /* Clear any previous error */
    wp_set_error(NULL);

    /* Allocate the main structure */
    WinPatina* wp = (WinPatina*)malloc(sizeof(WinPatina));
    if (wp == NULL) {
        wp_set_error("Failed to allocate memory for WinPatina");
        return NULL;
    }

    /* Zero-initialise */
    memset(wp, 0, sizeof(*wp));

    /* Copy configuration (or use defaults if NULL) */
    if (config != NULL) {
        wp->config = *config;
    } else {
        /* Default configuration */
        memset(&wp->config, 0, sizeof(wp->config));
        wp->config.enable_utf8 = true;
    }

    /* Get console handles */
    wp->hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    wp->hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);

    if (wp->hConsoleOutput == INVALID_HANDLE_VALUE) {
        wp_set_error("Failed to get console output handle");
        free(wp);
        return NULL;
    }

    /* Save original console modes (to restore on cleanup) */
    if (!GetConsoleMode(wp->hConsoleOutput, &wp->original_output_mode)) {
        wp_set_error_win32("Failed to get console output mode", GetLastError());
        free(wp);
        return NULL;
    }

    if (wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        GetConsoleMode(wp->hConsoleInput, &wp->original_input_mode);
    }

    /* Save original codepages */
    wp->original_output_cp = GetConsoleOutputCP();
    wp->original_input_cp = GetConsoleCP();

    /* Run capability detection */
    if (!wp_detect_capabilities(&wp->caps, &wp->config)) {
        /* Error already set by wp_detect_capabilities */
        free(wp);
        return NULL;
    }

    /* Set UTF-8 codepage if requested and available */
    if (wp->config.enable_utf8 && (wp->caps.flags & WP_CAP_UTF8_CODEPAGE)) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }

    /*
     * Set up the translation pipeline if we're in Win32 translation mode.
     * In passthrough mode the pipeline is not needed — VT sequences
     * go straight to the terminal.
     */
    if (wp->caps.mode == WP_MODE_WIN32_TRANSLATION ||
        wp->caps.mode == WP_MODE_HYBRID) {

        WORD default_attrs = 0x07;  /* White on black fallback */

        /* Use the console's actual default attributes if available */
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(wp->hConsoleOutput, &csbi)) {
            default_attrs = csbi.wAttributes;
        }

        bool has_lvb = (wp->caps.flags & WP_CAP_UNDERSCORE) != 0;

        /* Screen buffer */
        wp->screen = wp_screen_create(
            wp->caps.screen_width, wp->caps.screen_height, default_attrs);
        if (wp->screen == NULL) {
            wp_set_error("Failed to create screen buffer");
            free(wp);
            return NULL;
        }

        /* Dispatch (parser callbacks -> screen operations) */
        wp_dispatch_init(&wp->dispatch, wp->screen, default_attrs, has_lvb);

        /* VT parser */
        wp_vt_parser_init(&wp->parser, NULL);
        wp_dispatch_attach(&wp->dispatch, &wp->parser);

        /* Renderer */
        if (!wp_renderer_init(&wp->renderer, wp->hConsoleOutput, wp->screen)) {
            wp_set_error("Failed to initialise renderer");
            wp_screen_destroy(wp->screen);
            wp->screen = NULL;
            free(wp);
            return NULL;
        }

        /* Input handler */
        wp_input_init(&wp->input);

        /* Process manager (initialised but not spawned yet) */
        wp_process_init(&wp->process);

        wp->pipeline_ready = true;
    }

    return wp;
}

void wp_destroy(WinPatina* wp)
{
    if (wp == NULL) {
        return;
    }

    /* Tear down the pipeline */
    if (wp->pipeline_ready) {
        /* Terminate child if still running */
        if (wp_process_is_running(&wp->process)) {
            wp_process_terminate(&wp->process, 1);
        }
        wp_process_destroy(&wp->process);

        wp_renderer_destroy(&wp->renderer);

        if (wp->screen != NULL) {
            wp_screen_destroy(wp->screen);
            wp->screen = NULL;
        }

        wp->pipeline_ready = false;
    }

    /* Restore original console output mode */
    if (wp->hConsoleOutput != NULL && wp->hConsoleOutput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(wp->hConsoleOutput, wp->original_output_mode);
    }

    /* Restore original console input mode */
    if (wp->hConsoleInput != NULL && wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(wp->hConsoleInput, wp->original_input_mode);
    }

    /* Restore original codepages */
    if (wp->original_output_cp != 0) {
        SetConsoleOutputCP(wp->original_output_cp);
    }
    if (wp->original_input_cp != 0) {
        SetConsoleCP(wp->original_input_cp);
    }

    /* Free the structure */
    free(wp);
}

/*============================================================================
 * Query Functions
 *============================================================================*/

WinPatinaMode wp_get_mode(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_MODE_WIN32_TRANSLATION;  /* Safe default */
    }
    return wp->caps.mode;
}

WinPatinaWindowsVersion wp_get_windows_version(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_WIN_2000;  /* Safe default */
    }
    return wp->caps.os_version;
}

WinPatinaTerminalType wp_get_terminal_type(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_TERM_UNKNOWN;
    }
    return wp->caps.terminal;
}

uint32_t wp_get_capabilities(WinPatina* wp)
{
    if (wp == NULL) {
        return 0;
    }
    return wp->caps.flags;
}

void wp_get_screen_size(WinPatina* wp, int* width, int* height)
{
    if (wp == NULL) {
        if (width != NULL) *width = 80;
        if (height != NULL) *height = 25;
        return;
    }

    if (width != NULL) {
        *width = wp->caps.screen_width;
    }
    if (height != NULL) {
        *height = wp->caps.screen_height;
    }
}

